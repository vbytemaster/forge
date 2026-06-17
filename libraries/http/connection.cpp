module;

#include <chrono>
#include <coroutine>
#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <mutex>
#include <utility>
#include <vector>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

module fcl.http.connection;

import fcl.asio.runtime;
import fcl.http.body;
import fcl.http.exceptions;

namespace fcl::http {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
using tcp = asio::ip::tcp;
using asio::awaitable;
using asio::use_awaitable;

bool connection_reset_error(const boost::system::error_code& error) {
   return error == asio::error::broken_pipe || error == asio::error::connection_reset || error == asio::error::eof ||
          error == boost::beast::http::error::end_of_stream;
}

bool timeout_error(const boost::system::error_code& error) {
   return error == beast::error::timeout || error == asio::error::timed_out;
}

bool cancellation_error(const boost::system::error_code& error) {
   return error == asio::error::operation_aborted;
}

bool idempotent_method(method method_value) {
   return method_value == method::get || method_value == method::head || method_value == method::put ||
          method_value == method::delete_ || method_value == method::options;
}

void ensure_host_header(request& request_value, const base_url& endpoint) {
   if (request_value.find(field::host) == request_value.end()) {
      request_value.set(field::host, endpoint.host);
   }
}

response make_header_response(const beast_http::response_parser<beast_http::buffer_body>& parser) {
   const auto& source = parser.get();
   auto output = response{source.result(), source.version()};
   output.keep_alive(source.keep_alive());
   for (const auto& field_value : source) {
      output.insert(field_value.name_string(), field_value.value());
   }
   return output;
}

template <typename Stream> class beast_response_body_source final : public body_reader::source {
 public:
   beast_response_body_source(Stream stream, beast::flat_buffer buffer,
                              std::shared_ptr<beast_http::response_parser<beast_http::buffer_body>> parser,
                              std::chrono::milliseconds timeout)
       : stream_(std::move(stream)), buffer_(std::move(buffer)), parser_(std::move(parser)), timeout_(timeout) {}

   ~beast_response_body_source() override {
      auto ignored = boost::system::error_code{};
      beast::get_lowest_layer(stream_).socket().shutdown(tcp::socket::shutdown_both, ignored);
      beast::get_lowest_layer(stream_).socket().close(ignored);
   }

   awaitable<std::optional<body_chunk>> async_read() override {
      if (parser_->is_done()) {
         co_return std::nullopt;
      }

      for (;;) {
         auto storage = std::vector<std::byte>(64U * 1024U);
         auto& body = parser_->get().body();
         body.data = storage.data();
         body.size = storage.size();

         beast::get_lowest_layer(stream_).expires_after(timeout_);
         auto [read_error, bytes] =
            co_await beast_http::async_read_some(stream_, buffer_, *parser_, asio::as_tuple(use_awaitable));
         static_cast<void>(bytes);

         const auto produced = storage.size() - body.size;
         if (read_error == beast_http::error::need_buffer) {
            read_error = {};
         }
         if (read_error) {
            throw boost::system::system_error{read_error};
         }

         bytes_read_ += produced;
         if (produced != 0U) {
            storage.resize(produced);
            co_return body_chunk{.bytes = std::move(storage)};
         }
         if (parser_->is_done()) {
            co_return std::nullopt;
         }
      }
   }

   [[nodiscard]] std::uint64_t bytes_read() const noexcept override {
      return bytes_read_;
   }

 private:
   Stream stream_;
   beast::flat_buffer buffer_;
   std::shared_ptr<beast_http::response_parser<beast_http::buffer_body>> parser_;
   std::chrono::milliseconds timeout_;
   std::uint64_t bytes_read_ = 0;
};

template <typename Stream>
awaitable<void> write_streaming_request(Stream& stream,
                                        const request& request_value,
                                        body_reader& body,
                                        std::chrono::milliseconds timeout) {
   auto message = beast_http::request<beast_http::buffer_body>{request_value.method(), request_value.target(),
                                                               request_value.version()};
   for (const auto& field_value : request_value) {
      message.set(field_value.name_string(), field_value.value());
   }
   message.keep_alive(request_value.keep_alive());
   message.chunked(true);

   auto serializer = beast_http::request_serializer<beast_http::buffer_body>{message};
   serializer.split(true);
   beast::get_lowest_layer(stream).expires_after(timeout);
   co_await beast_http::async_write_header(stream, serializer, use_awaitable);

   while (auto chunk = co_await body.async_read()) {
      auto& body_value = message.body();
      body_value.data = chunk->bytes.data();
      body_value.size = chunk->bytes.size();
      body_value.more = true;
      beast::get_lowest_layer(stream).expires_after(timeout);
      auto [body_error, body_bytes] =
         co_await beast_http::async_write(stream, serializer, asio::as_tuple(use_awaitable));
      static_cast<void>(body_bytes);
      if (body_error && body_error != beast_http::error::need_buffer) {
         throw boost::system::system_error{body_error};
      }
   }

   auto& body_value = message.body();
   body_value.data = nullptr;
   body_value.size = 0;
   body_value.more = false;
   beast::get_lowest_layer(stream).expires_after(timeout);
   auto [final_error, final_bytes] =
      co_await beast_http::async_write(stream, serializer, asio::as_tuple(use_awaitable));
   static_cast<void>(final_bytes);
   if (final_error && final_error != beast_http::error::need_buffer) {
      throw boost::system::system_error{final_error};
   }
}

} // namespace

struct connection::impl {
   struct queued_request {
      explicit queued_request(asio::io_context& context)
          : completion_timer(context, (std::chrono::steady_clock::time_point::max)()) {}

      fcl::http::request request_value;
      request_options options;
      boost::asio::steady_timer completion_timer;
      mutable std::mutex completion_mutex;
      std::optional<response> result;
      std::exception_ptr error;
      bool completed = false;

      void complete_response(response response_value) {
         {
            const auto lock = std::scoped_lock{completion_mutex};
            result = std::move(response_value);
            completed = true;
         }
         completion_timer.cancel();
      }

      void complete_error(std::exception_ptr error_value) {
         {
            const auto lock = std::scoped_lock{completion_mutex};
            error = std::move(error_value);
            completed = true;
         }
         completion_timer.cancel();
      }

      bool is_completed() const {
         const auto lock = std::scoped_lock{completion_mutex};
         return completed;
      }

      response take_result() {
         const auto lock = std::scoped_lock{completion_mutex};
         if (error) {
            std::rethrow_exception(error);
         }
         if (!result.has_value()) {
            throw std::logic_error{"http request completed without response"};
         }
         return std::move(*result);
      }
   };

   explicit impl(fcl::asio::runtime& runtime_value, base_url endpoint_value)
       : runtime(runtime_value), endpoint(std::move(endpoint_value)), strand(asio::make_strand(runtime.context())),
         resolver(strand), ssl_context(asio::ssl::context::tls_client) {
      ssl_context.set_default_verify_paths();
      ssl_context.set_verify_mode(asio::ssl::verify_peer);
   }

   awaitable<void> ensure_plain_connected(std::chrono::milliseconds timeout) {
      if (plain_stream && plain_connected) {
         co_return;
      }

      auto results = co_await resolver.async_resolve(endpoint.host, endpoint.port, use_awaitable);
      auto stream = beast::tcp_stream{strand};
      stream.expires_after(timeout);
      co_await stream.async_connect(results, use_awaitable);
      plain_stream = std::make_unique<beast::tcp_stream>(std::move(stream));
      if (plain_connected_once) {
         record_reconnect();
      }
      plain_connected_once = true;
      plain_connected = true;
   }

   awaitable<void> ensure_tls_connected(std::chrono::milliseconds timeout) {
      if (tls_stream && tls_connected) {
         co_return;
      }

      auto results = co_await resolver.async_resolve(endpoint.host, endpoint.port, use_awaitable);

      auto stream = beast::ssl_stream<beast::tcp_stream>{strand, ssl_context};
      if (!SSL_set_tlsext_host_name(stream.native_handle(), endpoint.host.c_str())) {
         throw exceptions::internal{"failed to configure TLS host name"};
      }
      stream.set_verify_callback(asio::ssl::host_name_verification(endpoint.host));

      beast::get_lowest_layer(stream).expires_after(timeout);
      co_await beast::get_lowest_layer(stream).async_connect(results, use_awaitable);
      co_await stream.async_handshake(asio::ssl::stream_base::client, use_awaitable);
      tls_stream = std::make_unique<beast::ssl_stream<beast::tcp_stream>>(std::move(stream));
      if (tls_connected_once) {
         record_reconnect();
      }
      tls_connected_once = true;
      tls_connected = true;
   }

   awaitable<response> do_plain_request(fcl::http::request request_value, std::chrono::milliseconds timeout) {
      co_await ensure_plain_connected(timeout);

      ensure_host_header(request_value, endpoint);
      beast::get_lowest_layer(*plain_stream).expires_after(timeout);
      co_await boost::beast::http::async_write(*plain_stream, request_value, use_awaitable);

      buffer.consume(buffer.size());
      auto response_value = response{};
      if (request_value.method() == method::head) {
         auto parser = boost::beast::http::response_parser<string_body>{};
         parser.skip(true);
         co_await boost::beast::http::async_read(*plain_stream, buffer, parser, use_awaitable);
         response_value = parser.release();
      } else {
         co_await boost::beast::http::async_read(*plain_stream, buffer, response_value, use_awaitable);
      }

      if (!response_value.keep_alive()) {
         close_plain();
      }
      record_status(response_value);
      co_return response_value;
   }

   awaitable<response> do_tls_request(fcl::http::request request_value, std::chrono::milliseconds timeout) {
      co_await ensure_tls_connected(timeout);

      ensure_host_header(request_value, endpoint);
      beast::get_lowest_layer(*tls_stream).expires_after(timeout);
      co_await boost::beast::http::async_write(*tls_stream, request_value, use_awaitable);

      buffer.consume(buffer.size());
      auto response_value = response{};
      if (request_value.method() == method::head) {
         auto parser = boost::beast::http::response_parser<string_body>{};
         parser.skip(true);
         co_await boost::beast::http::async_read(*tls_stream, buffer, parser, use_awaitable);
         response_value = parser.release();
      } else {
         co_await boost::beast::http::async_read(*tls_stream, buffer, response_value, use_awaitable);
      }

      if (!response_value.keep_alive()) {
         close_tls();
      }
      record_status(response_value);
      co_return response_value;
   }

   awaitable<response> do_plain_streaming_request(fcl::http::request request_value,
                                                  body_reader body,
                                                  std::chrono::milliseconds timeout) {
      auto results = co_await resolver.async_resolve(endpoint.host, endpoint.port, use_awaitable);
      auto stream = beast::tcp_stream{strand};
      stream.expires_after(timeout);
      co_await stream.async_connect(results, use_awaitable);

      ensure_host_header(request_value, endpoint);
      co_await write_streaming_request(stream, request_value, body, timeout);

      auto stream_buffer = beast::flat_buffer{};
      auto response_value = response{};
      stream.expires_after(timeout);
      co_await beast_http::async_read(stream, stream_buffer, response_value, use_awaitable);
      record_status(response_value);
      co_return response_value;
   }

   awaitable<response> do_tls_streaming_request(fcl::http::request request_value,
                                                body_reader body,
                                                std::chrono::milliseconds timeout) {
      auto results = co_await resolver.async_resolve(endpoint.host, endpoint.port, use_awaitable);
      auto stream = beast::ssl_stream<beast::tcp_stream>{strand, ssl_context};
      if (!SSL_set_tlsext_host_name(stream.native_handle(), endpoint.host.c_str())) {
         throw exceptions::internal{"failed to configure TLS host name"};
      }
      stream.set_verify_callback(asio::ssl::host_name_verification(endpoint.host));

      beast::get_lowest_layer(stream).expires_after(timeout);
      co_await beast::get_lowest_layer(stream).async_connect(results, use_awaitable);
      co_await stream.async_handshake(asio::ssl::stream_base::client, use_awaitable);

      ensure_host_header(request_value, endpoint);
      co_await write_streaming_request(stream, request_value, body, timeout);

      auto stream_buffer = beast::flat_buffer{};
      auto response_value = response{};
      beast::get_lowest_layer(stream).expires_after(timeout);
      co_await beast_http::async_read(stream, stream_buffer, response_value, use_awaitable);
      record_status(response_value);
      co_return response_value;
   }

   awaitable<response_stream> do_plain_stream_request(fcl::http::request request_value,
                                                      std::optional<body_reader> body,
                                                      std::chrono::milliseconds timeout) {
      auto results = co_await resolver.async_resolve(endpoint.host, endpoint.port, use_awaitable);
      auto stream = beast::tcp_stream{strand};
      stream.expires_after(timeout);
      co_await stream.async_connect(results, use_awaitable);

      ensure_host_header(request_value, endpoint);
      if (body.has_value()) {
         co_await write_streaming_request(stream, request_value, *body, timeout);
      } else {
         stream.expires_after(timeout);
         co_await beast_http::async_write(stream, request_value, use_awaitable);
      }

      auto stream_buffer = beast::flat_buffer{};
      auto parser = std::make_shared<beast_http::response_parser<beast_http::buffer_body>>();
      if (request_value.method() == method::head) {
         parser->skip(true);
      }
      stream.expires_after(timeout);
      co_await beast_http::async_read_header(stream, stream_buffer, *parser, use_awaitable);
      auto head = make_header_response(*parser);
      record_status(head);
      auto source = std::make_shared<beast_response_body_source<beast::tcp_stream>>(
         std::move(stream), std::move(stream_buffer), std::move(parser), timeout);
      co_return response_stream{.head = std::move(head), .body = body_reader{std::move(source)}};
   }

   awaitable<response_stream> do_tls_stream_request(fcl::http::request request_value,
                                                    std::optional<body_reader> body,
                                                    std::chrono::milliseconds timeout) {
      auto results = co_await resolver.async_resolve(endpoint.host, endpoint.port, use_awaitable);
      auto stream = beast::ssl_stream<beast::tcp_stream>{strand, ssl_context};
      if (!SSL_set_tlsext_host_name(stream.native_handle(), endpoint.host.c_str())) {
         throw exceptions::internal{"failed to configure TLS host name"};
      }
      stream.set_verify_callback(asio::ssl::host_name_verification(endpoint.host));

      beast::get_lowest_layer(stream).expires_after(timeout);
      co_await beast::get_lowest_layer(stream).async_connect(results, use_awaitable);
      co_await stream.async_handshake(asio::ssl::stream_base::client, use_awaitable);

      ensure_host_header(request_value, endpoint);
      if (body.has_value()) {
         co_await write_streaming_request(stream, request_value, *body, timeout);
      } else {
         beast::get_lowest_layer(stream).expires_after(timeout);
         co_await beast_http::async_write(stream, request_value, use_awaitable);
      }

      auto stream_buffer = beast::flat_buffer{};
      auto parser = std::make_shared<beast_http::response_parser<beast_http::buffer_body>>();
      if (request_value.method() == method::head) {
         parser->skip(true);
      }
      beast::get_lowest_layer(stream).expires_after(timeout);
      co_await beast_http::async_read_header(stream, stream_buffer, *parser, use_awaitable);
      auto head = make_header_response(*parser);
      record_status(head);
      auto source = std::make_shared<beast_response_body_source<beast::ssl_stream<beast::tcp_stream>>>(
         std::move(stream), std::move(stream_buffer), std::move(parser), timeout);
      co_return response_stream{.head = std::move(head), .body = body_reader{std::move(source)}};
   }

   awaitable<response> streaming_request(fcl::http::request request_value, body_reader body, request_options options) {
      record_started();
      try {
         if (endpoint.secure()) {
            auto result = co_await do_tls_streaming_request(std::move(request_value), std::move(body), options.timeout);
            record_completed();
            co_return result;
         }
         auto result = co_await do_plain_streaming_request(std::move(request_value), std::move(body), options.timeout);
         record_completed();
         co_return result;
      } catch (const boost::system::system_error& error) {
         record_system_error(error.code());
         throw;
      } catch (...) {
         record_failed();
         throw;
      }
   }

   awaitable<response_stream> stream_request(fcl::http::request request_value,
                                             std::optional<body_reader> body,
                                             request_options options) {
      record_started();
      try {
         if (endpoint.secure()) {
            auto result = co_await do_tls_stream_request(std::move(request_value), std::move(body), options.timeout);
            record_completed();
            co_return result;
         }
         auto result = co_await do_plain_stream_request(std::move(request_value), std::move(body), options.timeout);
         record_completed();
         co_return result;
      } catch (const boost::system::system_error& error) {
         record_system_error(error.code());
         throw;
      } catch (...) {
         record_failed();
         throw;
      }
   }

   awaitable<void> process_request(std::shared_ptr<queued_request> operation) {
      const auto original = operation->request_value;
      const auto options = operation->options;
      const auto may_retry = options.retry_idempotent && idempotent_method(original.method());
      auto attempt = std::uint32_t{0};

      for (;;) {
         auto should_retry = false;
         try {
            record_started();
            if (endpoint.secure()) {
               operation->complete_response(co_await do_tls_request(original, options.timeout));
            } else {
               operation->complete_response(co_await do_plain_request(original, options.timeout));
            }
            record_completed();
            break;
         } catch (const boost::system::system_error& error) {
            if (connection_reset_error(error.code())) {
               close_all();
            }
            record_system_error(error.code());
            if (may_retry && attempt < options.max_retries && connection_reset_error(error.code())) {
               should_retry = true;
            } else {
               operation->complete_error(std::current_exception());
            }
         } catch (...) {
            close_all();
            record_failed();
            operation->complete_error(std::current_exception());
            break;
         }

         if (should_retry) {
            ++attempt;
            record_retry();
            co_await sleep_for(options.retry_backoff);
            continue;
         }
         break;
      }

      processing = false;
      start_next();
   }

   void enqueue(std::shared_ptr<queued_request> operation) {
      requests.push_back(std::move(operation));
      record_queued();
      start_next();
   }

   void start_next() {
      if (processing || requests.empty()) {
         return;
      }

      processing = true;
      auto operation = requests.front();
      requests.pop_front();

      asio::co_spawn(strand, process_request(std::move(operation)), [](std::exception_ptr error) {
         if (error) {
            try {
               std::rethrow_exception(error);
            } catch (const std::exception&) {
            }
         }
      });
   }

   void close_plain() {
      if (!plain_stream) {
         return;
      }

      auto ignored = boost::system::error_code{};
      beast::get_lowest_layer(*plain_stream).socket().shutdown(tcp::socket::shutdown_both, ignored);
      beast::get_lowest_layer(*plain_stream).socket().close(ignored);
      plain_stream.reset();
      plain_connected = false;
   }

   void close_tls() {
      if (!tls_stream) {
         return;
      }

      auto ignored = boost::system::error_code{};
      beast::get_lowest_layer(*tls_stream).socket().shutdown(tcp::socket::shutdown_both, ignored);
      beast::get_lowest_layer(*tls_stream).socket().close(ignored);
      tls_stream.reset();
      tls_connected = false;
   }

   void close_all() {
      close_plain();
      close_tls();
   }

   awaitable<void> sleep_for(std::chrono::milliseconds delay) {
      if (delay.count() <= 0) {
         co_return;
      }
      auto timer = asio::steady_timer{strand};
      timer.expires_after(delay);
      co_await timer.async_wait(use_awaitable);
   }

   void record_queued() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.queued_requests;
      current_metrics.queue_depth = requests.size() + (processing ? 1U : 0U);
   }

   void record_started() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.started_requests;
      current_metrics.queue_depth = requests.size() + 1U;
   }

   void record_completed() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.completed_requests;
      current_metrics.queue_depth = requests.size();
   }

   void record_failed() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.failed_requests;
      current_metrics.queue_depth = requests.size();
   }

   void record_retry() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.retry_attempts;
   }

   void record_reconnect() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.reconnects;
   }

   void record_system_error(const boost::system::error_code& error) {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.failed_requests;
      if (timeout_error(error)) {
         ++current_metrics.timeouts;
      }
      if (cancellation_error(error)) {
         ++current_metrics.cancellations;
      }
      current_metrics.queue_depth = requests.size();
   }

   void record_status(const response& response_value) {
      const auto value = response_value.result_int();
      const auto lock = std::scoped_lock{metrics_mutex};
      if (value < 200) {
         ++current_metrics.status_1xx;
      } else if (value < 300) {
         ++current_metrics.status_2xx;
      } else if (value < 400) {
         ++current_metrics.status_3xx;
      } else if (value < 500) {
         ++current_metrics.status_4xx;
      } else {
         ++current_metrics.status_5xx;
      }
   }

   [[nodiscard]] connection_metrics metrics() const {
      const auto lock = std::scoped_lock{metrics_mutex};
      auto snapshot = current_metrics;
      snapshot.queue_depth = requests.size() + (processing ? 1U : 0U);
      return snapshot;
   }

   fcl::asio::runtime& runtime;
   base_url endpoint;
   asio::strand<asio::io_context::executor_type> strand;
   tcp::resolver resolver;
   beast::flat_buffer buffer;
   asio::ssl::context ssl_context;
   std::unique_ptr<beast::tcp_stream> plain_stream;
   std::unique_ptr<beast::ssl_stream<beast::tcp_stream>> tls_stream;
   bool plain_connected = false;
   bool tls_connected = false;
   bool plain_connected_once = false;
   bool tls_connected_once = false;
   bool processing = false;
   std::deque<std::shared_ptr<queued_request>> requests;
   mutable std::mutex metrics_mutex;
   connection_metrics current_metrics{};
};

connection::connection(fcl::asio::runtime& runtime, base_url endpoint)
    : impl_(std::make_unique<impl>(runtime, std::move(endpoint))) {}

connection::~connection() = default;

boost::asio::awaitable<response> connection::async_request(fcl::http::request request_value, request_options options) {
   auto operation = std::make_shared<impl::queued_request>(impl_->runtime.context());
   operation->request_value = std::move(request_value);
   operation->options = options;

   asio::post(impl_->strand, [impl = impl_.get(), operation]() mutable { impl->enqueue(std::move(operation)); });

   while (!operation->is_completed()) {
      auto error = boost::system::error_code{};
      co_await operation->completion_timer.async_wait(boost::asio::redirect_error(use_awaitable, error));
      static_cast<void>(error);
   }
   co_return operation->take_result();
}

boost::asio::awaitable<response> connection::async_streaming_request(fcl::http::request request_value,
                                                                     body_reader body,
                                                                     request_options options) {
   co_await asio::dispatch(impl_->strand, use_awaitable);
   co_return co_await impl_->streaming_request(std::move(request_value), std::move(body), options);
}

boost::asio::awaitable<response_stream> connection::async_stream_request(fcl::http::request request_value,
                                                                         request_options options) {
   co_await asio::dispatch(impl_->strand, use_awaitable);
   co_return co_await impl_->stream_request(std::move(request_value), std::nullopt, options);
}

boost::asio::awaitable<response_stream> connection::async_stream_request(fcl::http::request request_value,
                                                                         body_reader body,
                                                                         request_options options) {
   co_await asio::dispatch(impl_->strand, use_awaitable);
   co_return co_await impl_->stream_request(std::move(request_value), std::move(body), options);
}

connection_metrics connection::metrics() const {
   return impl_->metrics();
}

} // namespace fcl::http
