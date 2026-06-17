module;

#include <coroutine>
#include <algorithm>
#include <condition_variable>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

module fcl.http.server;

import fcl.asio.runtime;
import fcl.http.body;
import fcl.http.exceptions;
import fcl.http.route_context;
import fcl.http.stream;
import fcl.websocket.connection;

namespace fcl::http {
namespace detail {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
namespace beast_websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;
using asio::awaitable;
using asio::use_awaitable;

stream_limits limits_from(const server_config& config) {
   return stream_limits{
       .max_body_bytes = config.max_request_body_bytes,
       .read_timeout = config.read_timeout,
       .write_timeout = config.idle_timeout,
   };
}

request make_header_request(const beast_http::request_parser<beast_http::buffer_body>& parser) {
   const auto& source = parser.get();
   auto request_value = request{};
   request_value.method(source.method());
   request_value.target(source.target());
   request_value.version(source.version());
   request_value.keep_alive(source.keep_alive());
   for (const auto& field_value : source) {
      request_value.insert(field_value.name_string(), field_value.value());
   }
   return request_value;
}

void copy_headers(const response& source, beast_http::response<beast_http::buffer_body>& target) {
   for (const auto& field_value : source) {
      target.set(field_value.name_string(), field_value.value());
   }
}

class beast_body_reader_source final : public body_reader::source {
 public:
   beast_body_reader_source(beast::tcp_stream& stream, beast::flat_buffer& buffer,
                            beast_http::request_parser<beast_http::buffer_body>& parser, stream_limits limits)
       : stream_(stream), buffer_(buffer), parser_(parser), limits_(limits) {}

   awaitable<std::optional<body_chunk>> async_read() override {
      if (parser_.is_done()) {
         co_return std::nullopt;
      }

      const auto chunk_size = std::max<std::uint64_t>(1, limits_.max_chunk_bytes);
      for (;;) {
         auto storage = std::vector<std::byte>(static_cast<std::size_t>(chunk_size));
         auto& body = parser_.get().body();
         body.data = storage.data();
         body.size = storage.size();

         stream_.expires_after(limits_.read_timeout);
         auto [read_error, bytes] =
            co_await beast_http::async_read_some(stream_, buffer_, parser_, asio::as_tuple(use_awaitable));
         static_cast<void>(bytes);

         const auto produced = storage.size() - body.size;
         if (read_error == beast_http::error::need_buffer) {
            read_error = {};
         }
         if (read_error == beast_http::error::body_limit) {
            FCL_THROW_EXCEPTION(exceptions::payload_too_large, "HTTP request body is too large");
         }
         if (read_error) {
            throw boost::system::system_error{read_error};
         }

         bytes_read_ += produced;
         if (bytes_read_ > limits_.max_body_bytes) {
            FCL_THROW_EXCEPTION(exceptions::payload_too_large, "HTTP request body is too large");
         }

         if (produced != 0U) {
            storage.resize(produced);
            co_return body_chunk{.bytes = std::move(storage)};
         }
         if (parser_.is_done()) {
            co_return std::nullopt;
         }
      }
   }

   [[nodiscard]] std::uint64_t bytes_read() const noexcept override {
      return bytes_read_;
   }

 private:
   beast::tcp_stream& stream_;
   beast::flat_buffer& buffer_;
   beast_http::request_parser<beast_http::buffer_body>& parser_;
   stream_limits limits_;
   std::uint64_t bytes_read_ = 0;
};

class server_session : public std::enable_shared_from_this<server_session> {
 public:
   server_session(fcl::asio::runtime& runtime, beast::tcp_stream stream, server_config config, server_handler handler,
                  std::shared_ptr<router> router_value)
       : runtime_{runtime}, stream_(std::move(stream)), config_(std::move(config)), handler_(std::move(handler)),
         router_(std::move(router_value)) {}

   void cancel() {
      auto self = shared_from_this();
      asio::dispatch(stream_.get_executor(), [self] { self->cancel_on_executor(); });
   }

   awaitable<void> async_cancel() {
      auto self = shared_from_this();
      static_cast<void>(self);
      co_await asio::dispatch(stream_.get_executor(), use_awaitable);
      cancel_on_executor();
   }

   awaitable<void> run() {
      auto self = shared_from_this();
      static_cast<void>(self);

      auto first_request = true;
      for (;;) {
         buffer_.consume(buffer_.size());
         auto parser = beast_http::request_parser<beast_http::buffer_body>{};
         parser.body_limit(config_.max_request_body_bytes);
         parser.header_limit(static_cast<std::uint32_t>(
            std::min<std::uint64_t>(config_.max_header_bytes, std::numeric_limits<std::uint32_t>::max())));
         stream_.expires_after(first_request ? config_.read_timeout : config_.idle_timeout);
         auto [read_error, bytes] =
             co_await beast_http::async_read_header(stream_, buffer_, parser, asio::as_tuple(use_awaitable));
         static_cast<void>(bytes);

         if (read_error == asio::error::eof) {
            co_return;
         }
         if (read_error == beast_http::error::header_limit) {
            auto response_value = response{status::request_header_fields_too_large, 11};
            response_value.set(field::content_type, "text/plain");
            response_value.body() = "headers too large";
            response_value.prepare_payload();
            response_value.keep_alive(false);
            co_await write_response(response_value);
            break;
         }
         if (read_error == beast_http::error::body_limit) {
            auto response_value = response{status::payload_too_large, 11};
            response_value.set(field::content_type, "text/plain");
            response_value.body() = "payload too large";
            response_value.prepare_payload();
            response_value.keep_alive(false);
            co_await write_response(response_value);
            break;
         }
         if (read_error) {
            throw boost::system::system_error{read_error};
         }
         first_request = false;

         auto request_value = make_header_request(parser);
         auto context_storage = std::optional<route_context>{};
         auto invalid_target = false;
         try {
            context_storage.emplace(make_context(request_value));
         } catch (...) {
            invalid_target = true;
         }
         if (invalid_target) {
            auto response_value = make_text_response(request_value, status::bad_request, "bad request");
            response_value.version(request_value.version());
            response_value.keep_alive(false);
            co_await write_response(response_value);
            break;
         }
         auto& context = *context_storage;
         if (beast_websocket::is_upgrade(request_value)) {
            if (co_await try_upgrade(request_value, context)) {
               co_return;
            }
         }

         auto body_source = std::make_shared<beast_body_reader_source>(stream_, buffer_, parser, limits_from(config_));
         if (router_ && router_->can_handle_stream(context)) {
            auto stream_request_value = stream_request{.context = context, .body = body_reader{body_source}};
            stream_.expires_after(config_.idle_timeout);
            auto response_value = co_await router_->handle_stream(stream_request_value);
            response_value.head.version(request_value.version());
            response_value.head.keep_alive(request_value.keep_alive() && parser.is_done());
            co_await write_stream_response(response_value);
            if (!response_value.head.keep_alive()) {
               break;
            }
            continue;
         }

         auto body_error_response = std::optional<response>{};
         try {
            request_value.body() = co_await body_reader{body_source}.async_read_all();
         } catch (const exceptions::payload_too_large&) {
            auto response_value = make_text_response(request_value, status::payload_too_large, "payload too large");
            response_value.version(request_value.version());
            response_value.keep_alive(false);
            body_error_response = std::move(response_value);
         }
         if (body_error_response.has_value()) {
            co_await write_response(*body_error_response);
            break;
         }
         request_value.prepare_payload();
         context_storage.emplace(make_context(request_value));
         auto& buffered_context = *context_storage;

         stream_.expires_after(config_.idle_timeout);
         auto response_value = co_await handle_http(buffered_context);
         response_value.version(request_value.version());
         response_value.keep_alive(request_value.keep_alive());

         co_await write_response(response_value);
         if (!response_value.keep_alive()) {
            break;
         }
      }

      auto ignored = boost::system::error_code{};
      stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
   }

 private:
   awaitable<void> write_response(response& response_value) {
      auto [write_error, written] =
          co_await beast_http::async_write(stream_, response_value, asio::as_tuple(use_awaitable));
      static_cast<void>(written);
      if (write_error) {
         throw boost::system::system_error{write_error};
      }
   }

   awaitable<void> write_stream_response(stream_response& response_value) {
      if (!response_value.body) {
         co_await write_response(response_value.head);
         co_return;
      }

      auto message = beast_http::response<beast_http::buffer_body>{response_value.head.result(),
                                                                   response_value.head.version()};
      copy_headers(response_value.head, message);
      message.keep_alive(response_value.head.keep_alive());
      if (message.find(field::content_length) == message.end()) {
         message.chunked(true);
      }

      auto serializer = beast_http::response_serializer<beast_http::buffer_body>{message};
      serializer.split(true);
      auto [header_error, header_bytes] =
         co_await beast_http::async_write_header(stream_, serializer, asio::as_tuple(use_awaitable));
      static_cast<void>(header_bytes);
      if (header_error) {
         throw boost::system::system_error{header_error};
      }

      while (auto chunk = co_await response_value.body()) {
         auto& body = message.body();
         body.data = chunk->bytes.data();
         body.size = chunk->bytes.size();
         body.more = true;

         auto [body_error, body_bytes] =
            co_await beast_http::async_write(stream_, serializer, asio::as_tuple(use_awaitable));
         static_cast<void>(body_bytes);
         if (body_error && body_error != beast_http::error::need_buffer) {
            throw boost::system::system_error{body_error};
         }
      }

      auto& body = message.body();
      body.data = nullptr;
      body.size = 0;
      body.more = false;
      auto [final_error, final_bytes] =
         co_await beast_http::async_write(stream_, serializer, asio::as_tuple(use_awaitable));
      static_cast<void>(final_bytes);
      if (final_error && final_error != beast_http::error::need_buffer) {
         throw boost::system::system_error{final_error};
      }
   }

   route_context make_context(const request& request_value) const {
      try {
         auto context = make_route_context(request_value);
         context.runtime = &runtime_;
         return context;
      } catch (...) {
         throw std::invalid_argument{"invalid HTTP request target"};
      }
   }

   awaitable<response> handle_http(route_context& context) const {
      try {
         if (router_) {
            co_return co_await router_->handle(context);
         }
         co_return co_await handler_(context);
      } catch (const std::invalid_argument&) {
         co_return make_text_response(context.request, status::bad_request, "bad request");
      } catch (...) {
         co_return make_text_response(context.request, status::internal_server_error, "internal server error");
      }
   }

   awaitable<bool> try_upgrade(const request& request_value, route_context& context) {
      if (!router_) {
         co_return false;
      }

      auto handler = router_->match_websocket(context);
      if (!handler.has_value()) {
         co_return false;
      }

      auto connection = fcl::websocket::connection::create(std::move(stream_));
      co_await connection->accept(request_value);
      (*handler)(connection);
      connection->start_read_loop();
      co_return true;
   }

   void cancel_on_executor() {
      auto ignored = boost::system::error_code{};
      stream_.socket().cancel(ignored);
      stream_.socket().shutdown(tcp::socket::shutdown_both, ignored);
      stream_.socket().close(ignored);
   }

   fcl::asio::runtime& runtime_;
   beast::tcp_stream stream_;
   server_config config_;
   beast::flat_buffer buffer_;
   server_handler handler_;
   std::shared_ptr<router> router_;
};

} // namespace detail

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
using asio::awaitable;
using asio::use_awaitable;

struct server::impl {
   impl(fcl::asio::runtime& runtime_value, server_config config_value, server_handler handler_value,
        std::shared_ptr<router> router_value)
       : runtime(runtime_value), config(std::move(config_value)), handler(std::move(handler_value)),
         router_value(std::move(router_value)), acceptor(asio::make_strand(runtime.context())) {}

   awaitable<void> accept_loop() {
      for (;;) {
         auto session_strand = asio::make_strand(runtime.context());
         auto socket = tcp::socket{session_strand};
         auto [error] = co_await acceptor.async_accept(socket, asio::as_tuple(use_awaitable));
         if (error == asio::error::operation_aborted) {
            co_return;
         }
         if (error) {
            throw boost::system::system_error{error};
         }

         auto client = std::make_shared<detail::server_session>(runtime, beast::tcp_stream{std::move(socket)}, config,
                                                                handler, router_value);
         remember_session(client);
         asio::co_spawn(session_strand, client->run(), [](std::exception_ptr error) {
            if (error) {
               try {
                  std::rethrow_exception(error);
               } catch (const std::exception&) {
               }
            }
         });
      }
   }

   fcl::asio::runtime& runtime;
   server_config config;
   server_handler handler;
   std::shared_ptr<router> router_value;
   tcp::acceptor acceptor;
   std::vector<std::weak_ptr<detail::server_session>> sessions;
   bool started = false;

   void prune_sessions() {
      sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
                                    [](const std::weak_ptr<detail::server_session>& session) {
                                       return session.expired();
                                    }),
                     sessions.end());
   }

   void remember_session(const std::shared_ptr<detail::server_session>& session) {
      prune_sessions();
      sessions.push_back(session);
   }

   std::vector<std::shared_ptr<detail::server_session>> active_sessions() {
      auto active = std::vector<std::shared_ptr<detail::server_session>>{};
      for (const auto& session : sessions) {
         if (auto locked = session.lock()) {
            active.push_back(std::move(locked));
         }
      }
      sessions.clear();
      sessions.reserve(active.size());
      for (const auto& session : active) {
         sessions.push_back(session);
      }
      return active;
   }

   void cancel_sessions() {
      for (auto& session : active_sessions()) {
         session->cancel();
      }
      sessions.clear();
   }

   awaitable<void> async_cancel_sessions() {
      for (auto& session : active_sessions()) {
         co_await session->async_cancel();
      }
      sessions.clear();
   }

   void start_on_executor() {
      if (started) {
         return;
      }

      const auto address = asio::ip::make_address(config.bind_address);
      auto endpoint = tcp::endpoint{address, config.port};

      acceptor.open(endpoint.protocol());
      acceptor.set_option(asio::socket_base::reuse_address(true));
      acceptor.bind(endpoint);
      acceptor.listen(asio::socket_base::max_listen_connections);
      started = true;

      asio::co_spawn(acceptor.get_executor(), accept_loop(), [](std::exception_ptr error) {
         if (error) {
            try {
               std::rethrow_exception(error);
            } catch (const std::exception&) {
            }
         }
      });
   }

   void stop_on_executor() {
      if (!started) {
         return;
      }
      auto ignored = boost::system::error_code{};
      acceptor.cancel(ignored);
      acceptor.close(ignored);
      cancel_sessions();
      started = false;
   }

   awaitable<void> async_stop_on_executor() {
      if (!started) {
         co_return;
      }
      auto ignored = boost::system::error_code{};
      acceptor.cancel(ignored);
      acceptor.close(ignored);
      started = false;
      co_await async_cancel_sessions();
   }
};

server::server(fcl::asio::runtime& runtime, server_config config, server_handler handler)
    : impl_(std::make_unique<impl>(runtime, std::move(config), std::move(handler), nullptr)) {}

server::server(fcl::asio::runtime& runtime, server_config config, router router_value)
    : impl_(std::make_unique<impl>(runtime, std::move(config), server_handler{},
                                   std::make_shared<router>(std::move(router_value)))) {}

server::~server() {
   stop();
}

void server::start() {
   struct start_state {
      std::mutex mutex;
      std::condition_variable ready;
      bool done = false;
      std::exception_ptr error;
   };

   auto state = std::make_shared<start_state>();
   asio::dispatch(impl_->acceptor.get_executor(), [impl = impl_.get(), state] {
      auto error = std::exception_ptr{};
      try {
         impl->start_on_executor();
      } catch (...) {
         error = std::current_exception();
      }
      {
         const auto lock = std::scoped_lock{state->mutex};
         state->error = std::move(error);
         state->done = true;
      }
      state->ready.notify_all();
   });

   auto lock = std::unique_lock{state->mutex};
   state->ready.wait(lock, [&] { return state->done; });
   if (state->error) {
      std::rethrow_exception(state->error);
   }
}

void server::stop() {
   if (!impl_ || !impl_->started) {
      return;
   }

   struct stop_state {
      std::mutex mutex;
      std::condition_variable ready;
      bool done = false;
      std::exception_ptr error;
   };

   auto state = std::make_shared<stop_state>();
   asio::co_spawn(
      impl_->acceptor.get_executor(),
      impl_->async_stop_on_executor(),
      [state](std::exception_ptr error) {
         {
            const auto lock = std::scoped_lock{state->mutex};
            state->error = std::move(error);
            state->done = true;
         }
         state->ready.notify_all();
      });

   auto lock = std::unique_lock{state->mutex};
   state->ready.wait(lock, [&] { return state->done; });
   if (state->error) {
      std::rethrow_exception(state->error);
   }
}

boost::asio::awaitable<void> server::async_start() {
   co_await asio::dispatch(impl_->acceptor.get_executor(), use_awaitable);
   impl_->start_on_executor();
}

boost::asio::awaitable<void> server::async_stop() {
   if (!impl_) {
      co_return;
   }
   co_await asio::dispatch(impl_->acceptor.get_executor(), use_awaitable);
   co_await impl_->async_stop_on_executor();
}

std::uint16_t server::port() const {
   if (!impl_->acceptor.is_open()) {
      return 0;
   }
   return impl_->acceptor.local_endpoint().port();
}

} // namespace fcl::http
