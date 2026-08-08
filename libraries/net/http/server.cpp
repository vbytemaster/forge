module;

#include <coroutine>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "details/router_server_access.hxx"
#include "details/stream_server_access.hxx"

#include <forge/exceptions/macros.hpp>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

module forge.net.http.server;

import forge.asio.exceptions;
import forge.asio.runtime;
import forge.net.http.body;
import forge.net.http.exceptions;
import forge.net.http.negotiation;
import forge.net.http.route_context;
import forge.net.http.stream;
import forge.net.websocket.connection;

namespace forge::net::http {
namespace detail {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
namespace beast_websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;
using asio::awaitable;
using asio::use_awaitable;

bool same_header_name(std::string_view left, std::string_view right) noexcept {
   if (left.size() != right.size()) {
      return false;
   }
   for (auto index = std::size_t{0}; index != left.size(); ++index) {
      if (std::tolower(static_cast<unsigned char>(left[index])) !=
          std::tolower(static_cast<unsigned char>(right[index]))) {
         return false;
      }
   }
   return true;
}

stream_limits limits_from(const server_config& config) {
   return stream_limits{
       .max_body_bytes = config.max_request_body_bytes,
       .read_timeout = config.read_timeout,
       .write_timeout = config.idle_timeout,
   };
}

method to_http_method(beast_http::verb value) noexcept {
   switch (value) {
   case beast_http::verb::delete_:
      return method::delete_;
   case beast_http::verb::get:
      return method::get;
   case beast_http::verb::head:
      return method::head;
   case beast_http::verb::options:
      return method::options;
   case beast_http::verb::patch:
      return method::patch;
   case beast_http::verb::post:
      return method::post;
   case beast_http::verb::put:
      return method::put;
   default:
      return method::unknown;
   }
}

beast_http::status to_beast_status(status value) noexcept {
   return static_cast<beast_http::status>(static_cast<unsigned>(value));
}

request make_header_request(const beast_http::request_parser<beast_http::buffer_body>& parser) {
   const auto& source = parser.get();
   auto request_value = request{};
   request_value.method(to_http_method(source.method()));
   request_value.target(std::string_view{source.target().data(), source.target().size()});
   request_value.version(source.version());
   request_value.keep_alive(source.keep_alive());
   for (const auto& field_value : source) {
      request_value.insert(field_value.name_string(), field_value.value());
   }
   return request_value;
}

void copy_headers(const response& source, beast_http::response<beast_http::buffer_body>& target) {
   for (const auto& field_value : source.headers()) {
      if (same_header_name(field_value.name, "Set-Cookie")) {
         target.insert(field_value.name, field_value.text);
      } else {
         target.set(field_value.name, field_value.text);
      }
   }
}

beast_http::response<beast_http::string_body> to_beast_response(const response& source) {
   auto target = beast_http::response<beast_http::string_body>{to_beast_status(source.result()), source.version()};
   for (const auto& header : source.headers()) {
      target.insert(header.name, header.text);
   }
   target.keep_alive(source.keep_alive());
   target.body() = source.body();
   return target;
}

bool expects_continue(const request& value) {
   const auto found = value.find(field::expect);
   return found != value.end() && normalize_token(found->value()) == "100-continue";
}

beast_http::request<beast_http::string_body>
to_websocket_request(const beast_http::request<beast_http::buffer_body>& source) {
   auto target = beast_http::request<beast_http::string_body>{source.method(), source.target(), source.version()};
   for (const auto& header : source) {
      target.insert(header.name_string(), header.value());
   }
   target.keep_alive(source.keep_alive());
   return target;
}

std::size_t request_buffer_limit(const server_config& config) noexcept {
   const auto size_limit = static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)());
   const auto body_limit = std::min(config.max_request_body_bytes, size_limit);
   const auto header_limit = std::min(config.max_header_bytes, size_limit - body_limit);
   return static_cast<std::size_t>(std::max<std::uint64_t>(1, body_limit + header_limit));
}

class request_read_ownership {
 public:
   explicit request_read_ownership(asio::any_io_executor executor)
       : changed_(std::move(executor), (std::chrono::steady_clock::time_point::max)()) {}

   awaitable<void> begin_body_read() {
      body_read_requested_ = true;
      notify();
      monitor_operation_cancellation_.emit(asio::cancellation_type::all);
      try {
         while (monitor_read_active_) {
            co_await wait_for_change();
         }
      } catch (...) {
         body_read_requested_ = false;
         notify();
         throw;
      }
      if (connection_closed_) {
         body_read_requested_ = false;
         notify();
         throw forge::asio::exceptions::canceled{"HTTP client disconnected while reading the request body"};
      }
      body_read_active_ = true;
      body_read_requested_ = false;
      notify();
   }

   void finish_body_read() noexcept {
      body_read_active_ = false;
      notify();
   }

   awaitable<bool> begin_monitor_read() {
      while (body_read_active_ || body_read_requested_) {
         if (monitor_stopping_) {
            co_return false;
         }
         co_await wait_for_change();
      }
      if (monitor_stopping_) {
         co_return false;
      }
      monitor_read_active_ = true;
      co_return true;
   }

   void finish_monitor_read() noexcept {
      monitor_read_active_ = false;
      notify();
   }

   void stop_monitor() {
      monitor_stopping_ = true;
      monitor_operation_cancellation_.emit(asio::cancellation_type::all);
      notify();
   }

   void mark_connection_closed() {
      connection_closed_ = true;
      stop_monitor();
   }

   void mark_peer_read_closed() {
      peer_read_closed_ = true;
      notify();
   }

   [[nodiscard]] bool peer_read_closed() const noexcept {
      return peer_read_closed_;
   }

   [[nodiscard]] bool can_read_next_request(bool has_buffered_input) const noexcept {
      return !peer_read_closed_ || has_buffered_input;
   }

   [[nodiscard]] bool body_read_requested() const noexcept {
      return body_read_requested_;
   }

   [[nodiscard]] bool monitor_stopping() const noexcept {
      return monitor_stopping_;
   }

   [[nodiscard]] asio::cancellation_slot monitor_cancellation_slot() noexcept {
      return monitor_operation_cancellation_.slot();
   }

   awaitable<bool> wait_for_monitor_retry() {
      if (monitor_stopping_) {
         co_return false;
      }
      co_await wait_for_change();
      co_return !monitor_stopping_;
   }

 private:
   awaitable<void> wait_for_change() {
      auto [error] = co_await changed_.async_wait(asio::as_tuple(use_awaitable));
      if (error && error != asio::error::operation_aborted) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP request read ownership wait failed",
                               forge::exceptions::ctx("reason", error.message()));
      }
   }

   void notify() noexcept {
      try {
         changed_.cancel();
      } catch (...) {
      }
   }

   asio::steady_timer changed_;
   asio::cancellation_signal monitor_operation_cancellation_;
   bool body_read_requested_ = false;
   bool body_read_active_ = false;
   bool monitor_read_active_ = false;
   bool monitor_stopping_ = false;
   bool connection_closed_ = false;
   bool peer_read_closed_ = false;
};

class body_read_scope {
 public:
   explicit body_read_scope(std::shared_ptr<request_read_ownership> ownership) : ownership_(std::move(ownership)) {}

   ~body_read_scope() {
      if (ownership_) {
         ownership_->finish_body_read();
      }
   }

   body_read_scope(const body_read_scope&) = delete;
   body_read_scope& operator=(const body_read_scope&) = delete;

 private:
   std::shared_ptr<request_read_ownership> ownership_;
};

class monitor_read_scope {
 public:
   explicit monitor_read_scope(std::shared_ptr<request_read_ownership> ownership) : ownership_(std::move(ownership)) {}

   ~monitor_read_scope() {
      ownership_->finish_monitor_read();
   }

   monitor_read_scope(const monitor_read_scope&) = delete;
   monitor_read_scope& operator=(const monitor_read_scope&) = delete;

 private:
   std::shared_ptr<request_read_ownership> ownership_;
};

class beast_body_reader_source final : public body_reader::source {
 public:
   beast_body_reader_source(beast::tcp_stream& stream, beast::flat_buffer& buffer,
                            beast_http::request_parser<beast_http::buffer_body>& parser, stream_limits limits,
                            bool send_continue, std::shared_ptr<request_read_ownership> read_ownership = {})
       : stream_(stream), buffer_(buffer), parser_(parser), limits_(limits), send_continue_(send_continue),
         read_ownership_(std::move(read_ownership)) {}

   awaitable<std::optional<body_chunk>> async_read() override {
      try {
         if (parser_.is_done()) {
            co_return std::nullopt;
         }
         co_await send_continue_if_needed();

         const auto chunk_size = std::max<std::uint64_t>(1, limits_.max_chunk_bytes);
         for (;;) {
            auto storage = std::vector<std::byte>(static_cast<std::size_t>(chunk_size));
            auto& body = parser_.get().body();
            body.data = storage.data();
            body.size = storage.size();

            stream_.expires_after(limits_.read_timeout);
            if (read_ownership_) {
               co_await read_ownership_->begin_body_read();
            }
            auto read_scope = body_read_scope{read_ownership_};
            auto [read_error, bytes] =
                co_await beast_http::async_read_some(stream_, buffer_, parser_, asio::as_tuple(use_awaitable));
            static_cast<void>(bytes);

            const auto produced = storage.size() - body.size;
            if (read_error == beast_http::error::need_buffer) {
               read_error = {};
            }
            if (read_error == beast_http::error::body_limit) {
               FORGE_THROW_EXCEPTION(exceptions::payload_too_large, "HTTP request body is too large");
            }
            if (read_error) {
               if (read_error == asio::error::operation_aborted || read_error == asio::error::eof ||
                   read_error == asio::error::connection_reset) {
                  throw forge::asio::exceptions::canceled{"HTTP request body read was canceled"};
               }
               if (read_error == beast::error::timeout || read_error == asio::error::timed_out) {
                  FORGE_THROW_EXCEPTION(exceptions::bad_request, "HTTP request body read timed out");
               }
               FORGE_THROW_EXCEPTION(exceptions::bad_request, "HTTP request body read failed",
                                     forge::exceptions::ctx("reason", read_error.message()));
            }

            bytes_read_ += produced;
            if (bytes_read_ > limits_.max_body_bytes) {
               FORGE_THROW_EXCEPTION(exceptions::payload_too_large, "HTTP request body is too large");
            }

            if (produced != 0U) {
               storage.resize(produced);
               co_return body_chunk{.bytes = std::move(storage)};
            }
            if (parser_.is_done()) {
               co_return std::nullopt;
            }
         }
      } catch (const forge::exceptions::base&) {
         throw;
      } catch (const boost::system::system_error& error) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP request body reader failed",
                               forge::exceptions::ctx("reason", error.what()));
      } catch (const std::exception& error) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP request body reader failed",
                               forge::exceptions::ctx("reason", error.what()));
      } catch (...) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP request body reader failed");
      }
   }

   [[nodiscard]] std::uint64_t bytes_read() const noexcept override {
      return bytes_read_;
   }

   [[nodiscard]] bool requires_continue_before_response() const noexcept override {
      return send_continue_ && !parser_.is_done();
   }

   [[nodiscard]] bool done() const noexcept {
      return parser_.is_done();
   }

   awaitable<void> send_continue_if_needed() {
      try {
         if (!send_continue_ || parser_.is_done()) {
            co_return;
         }
         send_continue_ = false;
         auto reply =
             beast_http::response<beast_http::empty_body>{beast_http::status::continue_, parser_.get().version()};
         reply.keep_alive(true);
         stream_.expires_after(limits_.write_timeout);
         auto [write_error, written] = co_await beast_http::async_write(stream_, reply, asio::as_tuple(use_awaitable));
         static_cast<void>(written);
         if (write_error) {
            if (write_error == asio::error::operation_aborted || write_error == asio::error::eof ||
                write_error == asio::error::connection_reset || write_error == asio::error::broken_pipe) {
               throw forge::asio::exceptions::canceled{"HTTP continue response was canceled"};
            }
            FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP continue response failed",
                                  forge::exceptions::ctx("reason", write_error.message()));
         }
      } catch (const forge::exceptions::base&) {
         throw;
      } catch (const boost::system::system_error& error) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP continue response failed",
                               forge::exceptions::ctx("reason", error.what()));
      } catch (const std::exception& error) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP continue response failed",
                               forge::exceptions::ctx("reason", error.what()));
      } catch (...) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP continue response failed");
      }
   }

 private:
   beast::tcp_stream& stream_;
   beast::flat_buffer& buffer_;
   beast_http::request_parser<beast_http::buffer_body>& parser_;
   stream_limits limits_;
   bool send_continue_ = false;
   std::uint64_t bytes_read_ = 0;
   std::shared_ptr<request_read_ownership> read_ownership_;
};

class server_session : public std::enable_shared_from_this<server_session> {
 public:
   server_session(forge::asio::runtime& runtime, beast::tcp_stream stream, server_config config, server_handler handler,
                  std::shared_ptr<router> router_value)
       : runtime_{runtime}, stream_(std::move(stream)), config_(std::move(config)),
         buffer_(request_buffer_limit(config_)),
         run_completion_(stream_.get_executor(), (std::chrono::steady_clock::time_point::max)()),
         handler_(std::move(handler)), router_(std::move(router_value)) {}

   void cancel() {
      auto self = shared_from_this();
      asio::dispatch(stream_.get_executor(), [self] { self->cancel_on_executor(); });
   }

   void cancel_after_runtime_stopped() {
      cancel_on_executor();
   }

   awaitable<void> async_cancel() {
      auto self = shared_from_this();
      static_cast<void>(self);
      co_await asio::dispatch(stream_.get_executor(), use_awaitable);
      cancel_on_executor();
      while (!run_completed_) {
         auto [error] = co_await run_completion_.async_wait(asio::as_tuple(use_awaitable));
         if (error && error != asio::error::operation_aborted) {
            throw boost::system::system_error{error};
         }
      }
   }

   awaitable<void> run() {
      auto self = shared_from_this();
      static_cast<void>(self);

      try {
         co_await run_loop();
      } catch (...) {
         complete_run();
         throw;
      }
      complete_run();
   }

 private:
   void complete_run() noexcept {
      run_completed_ = true;
      try {
         run_completion_.cancel();
      } catch (...) {
      }
   }

   awaitable<void> run_loop() {
      auto first_request = true;
      for (;;) {
         if (stopping_) {
            co_return;
         }
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
         if (beast_websocket::is_upgrade(parser.get())) {
            if (co_await try_upgrade(parser.get(), context)) {
               co_return;
            }
         }

         if (router_) {
            auto preflight_response = response{};
            if (detail::router_server_access::reject_without_body(*router_, context, preflight_response)) {
               preflight_response.version(request_value.version());
               preflight_response.keep_alive(request_value.keep_alive() && parser.is_done());
               co_await write_response(preflight_response);
               if (!preflight_response.keep_alive()) {
                  break;
               }
               continue;
            }
         }

         const auto stream_capable = router_ && router_->can_handle_stream(context);
         auto read_ownership = stream_capable ? std::make_shared<request_read_ownership>(stream_.get_executor())
                                              : std::shared_ptr<request_read_ownership>{};
         auto body_source = std::make_shared<beast_body_reader_source>(stream_, buffer_, parser, limits_from(config_),
                                                                       expects_continue(request_value), read_ownership);
         auto request_body_marker = std::make_shared<int>(0);
         auto request_body =
             detail::stream_server_access::mark_request_body(body_reader{body_source}, request_body_marker);
         if (stream_capable) {
            auto stream_request_value =
                detail::stream_server_access::make_request(context, std::move(request_body), request_body_marker);
            stream_.expires_after(config_.idle_timeout);
            auto keep_alive = co_await handle_owned_operation(
                handle_and_write_stream(std::move(stream_request_value), body_source, request_body_marker,
                                        read_ownership, request_value.version(), request_value.keep_alive()),
                read_ownership);
            if (!keep_alive.has_value()) {
               co_return;
            }
            if (!*keep_alive) {
               break;
            }
            continue;
         }

         auto body_error_response = std::optional<response>{};
         try {
            request_value.body() = co_await request_body.async_read_all();
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
         read_ownership = std::make_shared<request_read_ownership>(stream_.get_executor());
         auto response_value = co_await handle_owned_operation(handle_http(buffered_context), read_ownership);
         if (!response_value.has_value()) {
            co_return;
         }
         response_value->version(request_value.version());
         response_value->keep_alive(request_value.keep_alive() &&
                                    read_ownership->can_read_next_request(buffer_.size() != 0U));

         co_await write_response(*response_value);
         if (!response_value->keep_alive()) {
            break;
         }
      }

      auto ignored = boost::system::error_code{};
      stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
   }

 private:
   template <typename Result> struct owner_handler_state {
      owner_handler_state(asio::any_io_executor executor, std::shared_ptr<request_read_ownership> ownership)
          : owner_completion(executor, (std::chrono::steady_clock::time_point::max)()),
            monitor_completion(std::move(executor), (std::chrono::steady_clock::time_point::max)()),
            read_ownership(std::move(ownership)) {}

      void complete_owner(std::exception_ptr error_value, Result response_value) noexcept {
         try {
            owner_error = std::move(error_value);
            if (!owner_error) {
               owner_response.emplace(std::move(response_value));
            }
         } catch (...) {
            owner_error = std::current_exception();
            owner_response.reset();
         }
         owner_done = true;
         try {
            owner_completion.cancel();
         } catch (...) {
         }
      }

      void complete_monitor(std::exception_ptr error_value) noexcept {
         try {
            if (error_value && !owner_done) {
               mark_disconnected();
            }
         } catch (...) {
         }
         monitor_done = true;
         try {
            monitor_completion.cancel();
         } catch (...) {
         }
      }

      void mark_disconnected() {
         disconnected = true;
         read_ownership->mark_connection_closed();
         if (!owner_done) {
            owner_cancellation.emit(asio::cancellation_type::all);
         }
      }

      asio::cancellation_signal owner_cancellation;
      asio::steady_timer owner_completion;
      asio::steady_timer monitor_completion;
      std::shared_ptr<request_read_ownership> read_ownership;
      std::optional<Result> owner_response;
      std::exception_ptr owner_error;
      bool owner_done = false;
      bool monitor_done = false;
      bool disconnected = false;
   };

   awaitable<void> wait_for_completion(asio::steady_timer& completion, const bool& done) {
      while (!done) {
         auto [error] = co_await completion.async_wait(asio::as_tuple(use_awaitable));
         if (error && error != asio::error::operation_aborted) {
            FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP owner completion wait failed",
                                  forge::exceptions::ctx("reason", error.message()));
         }
      }
   }

   template <typename Result>
   awaitable<void> monitor_owner_disconnect(const std::shared_ptr<owner_handler_state<Result>>& state) {
      for (;;) {
         if (!(co_await state->read_ownership->begin_monitor_read())) {
            co_return;
         }

         auto read_error = boost::system::error_code{};
         auto bytes = std::size_t{0};
         auto buffer_exhausted = false;
         {
            auto read_scope = monitor_read_scope{state->read_ownership};
            const auto remaining = buffer_.max_size() - buffer_.size();
            if (remaining == 0U) {
               buffer_exhausted = true;
            } else {
               const auto read_size = std::min<std::size_t>(remaining, 64U * 1024U);
               auto destination = buffer_.prepare(read_size);
               auto [operation_error, transferred] = co_await stream_.socket().async_read_some(
                   destination, asio::bind_cancellation_slot(state->read_ownership->monitor_cancellation_slot(),
                                                             asio::as_tuple(use_awaitable)));
               read_error = operation_error;
               bytes = transferred;
               buffer_.commit(bytes);
            }
         }

         if (buffer_exhausted) {
            while (buffer_.size() == buffer_.max_size()) {
               if (!(co_await state->read_ownership->wait_for_monitor_retry())) {
                  co_return;
               }
            }
            continue;
         }
         if (read_error == asio::error::operation_aborted) {
            if (state->owner_done || state->read_ownership->monitor_stopping()) {
               co_return;
            }
            if (state->read_ownership->body_read_requested()) {
               continue;
            }
         } else if (read_error == asio::error::eof) {
            state->read_ownership->mark_peer_read_closed();
            co_return;
         } else if (!read_error && bytes != 0U) {
            continue;
         }
         state->mark_disconnected();
         co_return;
      }
   }

   template <typename Result>
   awaitable<std::optional<Result>> handle_owned_operation(awaitable<Result> operation,
                                                           std::shared_ptr<request_read_ownership> read_ownership) {
      auto state = std::make_shared<owner_handler_state<Result>>(stream_.get_executor(), std::move(read_ownership));
      auto self = shared_from_this();
      try {
         asio::co_spawn(stream_.get_executor(), monitor_owner_disconnect(state),
                        [self = std::move(self), state](std::exception_ptr error) mutable {
                           state->complete_monitor(std::move(error));
                        });
         asio::co_spawn(stream_.get_executor(), std::move(operation),
                        asio::bind_cancellation_slot(state->owner_cancellation.slot(),
                                                     [state](std::exception_ptr error, Result response_value) mutable {
                                                        state->complete_owner(std::move(error),
                                                                              std::move(response_value));
                                                     }));

         co_await wait_for_completion(state->owner_completion, state->owner_done);
         state->read_ownership->stop_monitor();
         co_await wait_for_completion(state->monitor_completion, state->monitor_done);

         if (state->disconnected) {
            co_return std::nullopt;
         }
         if (state->owner_error) {
            std::rethrow_exception(state->owner_error);
         }
         if (!state->owner_response.has_value()) {
            FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP owner handler completed without a response");
         }
         co_return std::move(state->owner_response);
      } catch (const forge::exceptions::base&) {
         state->read_ownership->stop_monitor();
         throw;
      } catch (const boost::system::system_error& error) {
         state->read_ownership->stop_monitor();
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP owner handler failed",
                               forge::exceptions::ctx("reason", error.what()));
      } catch (const std::exception& error) {
         state->read_ownership->stop_monitor();
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP owner handler failed",
                               forge::exceptions::ctx("reason", error.what()));
      } catch (...) {
         state->read_ownership->stop_monitor();
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP owner handler failed");
      }
   }

   awaitable<bool> handle_and_write_stream(stream_request request_value,
                                           const std::shared_ptr<beast_body_reader_source>& body_source,
                                           const std::shared_ptr<int>& request_body_marker,
                                           const std::shared_ptr<request_read_ownership>& read_ownership,
                                           unsigned version, bool request_keep_alive) {
      auto response_value = co_await router_->handle_stream(request_value);
      const auto request_body_deferred_to_response =
          detail::stream_server_access::response_body_uses_request(response_value, request_body_marker) &&
          !body_source->done();
      response_value.head.version(version);
      response_value.head.keep_alive(request_keep_alive && body_source->done() &&
                                     read_ownership->can_read_next_request(buffer_.size() != 0U));
      if (request_body_deferred_to_response) {
         co_await body_source->send_continue_if_needed();
      }
      co_await write_stream_response(response_value);
      co_return response_value.head.keep_alive();
   }

   awaitable<void> write_response(response& response_value) {
      auto beast_response = to_beast_response(response_value);
      stream_.expires_after(config_.idle_timeout);
      auto [write_error, written] =
          co_await beast_http::async_write(stream_, beast_response, asio::as_tuple(use_awaitable));
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

      auto message = beast_http::response<beast_http::buffer_body>{to_beast_status(response_value.head.result()),
                                                                   response_value.head.version()};
      copy_headers(response_value.head, message);
      message.keep_alive(response_value.head.keep_alive());
      if (message.find(field_name(field::content_length)) == message.end()) {
         message.chunked(true);
      }

      auto serializer = beast_http::response_serializer<beast_http::buffer_body>{message};
      serializer.split(true);
      stream_.expires_after(config_.idle_timeout);
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

         stream_.expires_after(config_.idle_timeout);
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
      stream_.expires_after(config_.idle_timeout);
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
      } catch (const exceptions::bad_request&) {
         throw;
      } catch (const std::exception& error) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "invalid HTTP request target",
                               forge::exceptions::ctx("reason", error.what()));
      } catch (...) {
         FORGE_THROW_EXCEPTION(exceptions::bad_request, "invalid HTTP request target");
      }
   }

   awaitable<response> handle_http(route_context& context) const {
      try {
         if (router_) {
            co_return co_await router_->handle(context);
         }
         co_return co_await handler_(context);
      } catch (const exceptions::bad_request&) {
         co_return make_text_response(context.request, status::bad_request, "bad request");
      } catch (...) {
         co_return make_text_response(context.request, status::internal_server_error, "internal server error");
      }
   }

   awaitable<bool> try_upgrade(const beast_http::request<beast_http::buffer_body>& request_value,
                               route_context& context) {
      if (!router_) {
         co_return false;
      }

      auto handler = router_->match_websocket(context);
      if (!handler.has_value()) {
         co_return false;
      }

      auto connection = forge::net::websocket::connection::create(std::move(stream_));
      auto websocket_request = to_websocket_request(request_value);
      co_await connection->accept(websocket_request);
      (*handler)(connection);
      connection->start_read_loop();
      co_return true;
   }

   void cancel_on_executor() {
      stopping_ = true;
      auto ignored = boost::system::error_code{};
      stream_.socket().cancel(ignored);
      stream_.socket().shutdown(tcp::socket::shutdown_both, ignored);
      stream_.socket().close(ignored);
   }

   forge::asio::runtime& runtime_;
   beast::tcp_stream stream_;
   server_config config_;
   beast::flat_buffer buffer_;
   asio::steady_timer run_completion_;
   server_handler handler_;
   std::shared_ptr<router> router_;
   bool run_completed_ = false;
   bool stopping_ = false;
};

} // namespace detail

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
using asio::awaitable;
using asio::use_awaitable;

struct server::impl : std::enable_shared_from_this<server::impl> {
   impl(forge::asio::runtime& runtime_value, server_config config_value, server_handler handler_value,
        std::shared_ptr<router> router_value)
       : runtime(runtime_value), config(std::move(config_value)), handler(std::move(handler_value)),
         router_value(std::move(router_value)), acceptor_executor(asio::make_strand(runtime.context())),
         acceptor(acceptor_executor),
         accept_loop_completion(acceptor_executor, (std::chrono::steady_clock::time_point::max)()),
         stop_completion(acceptor_executor, (std::chrono::steady_clock::time_point::max)()) {}

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
         if (stopping) {
            auto ignored = boost::system::error_code{};
            socket.close(ignored);
            co_return;
         }

         auto client = std::make_shared<detail::server_session>(runtime, beast::tcp_stream{std::move(socket)}, config,
                                                                handler, router_value);
         remember_session(client);
         asio::co_spawn(session_strand, client->run(), [client](std::exception_ptr error) {
            if (error) {
               try {
                  std::rethrow_exception(error);
               } catch (const std::exception&) {
               }
            }
         });
      }
   }

   forge::asio::runtime& runtime;
   server_config config;
   server_handler handler;
   std::shared_ptr<router> router_value;
   asio::strand<asio::io_context::executor_type> acceptor_executor;
   tcp::acceptor acceptor;
   asio::steady_timer accept_loop_completion;
   asio::steady_timer stop_completion;
   std::vector<std::weak_ptr<detail::server_session>> sessions;
   std::atomic_bool stopped = true;
   bool started = false;
   bool stopping = false;
   bool accept_loop_completed = true;

   void prune_sessions() {
      sessions.erase(
          std::remove_if(sessions.begin(), sessions.end(),
                         [](const std::weak_ptr<detail::server_session>& session) { return session.expired(); }),
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

   void cancel_sessions_after_runtime_stopped() {
      for (auto& session : active_sessions()) {
         session->cancel_after_runtime_stopped();
      }
      sessions.clear();
   }

   void begin_cancel_sessions() {
      for (auto& session : active_sessions()) {
         session->cancel();
      }
   }

   awaitable<void> async_cancel_sessions() {
      auto active = active_sessions();
      for (auto& session : active) {
         session->cancel();
      }
      for (auto& session : active) {
         co_await session->async_cancel();
      }
      sessions.clear();
   }

   awaitable<void> wait_until_stopped() {
      while (!stopped.load(std::memory_order_acquire)) {
         auto [error] = co_await stop_completion.async_wait(asio::as_tuple(use_awaitable));
         if (error && error != asio::error::operation_aborted) {
            throw boost::system::system_error{error};
         }
      }
   }

   awaitable<void> wait_until_accept_loop_completed() {
      while (!accept_loop_completed) {
         auto [error] = co_await accept_loop_completion.async_wait(asio::as_tuple(use_awaitable));
         if (error && error != asio::error::operation_aborted) {
            throw boost::system::system_error{error};
         }
      }
   }

   void complete_accept_loop() noexcept {
      accept_loop_completed = true;
      try {
         accept_loop_completion.cancel();
      } catch (...) {
      }
   }

   void complete_stop() noexcept {
      started = false;
      stopping = false;
      stopped.store(true, std::memory_order_release);
      try {
         stop_completion.cancel();
      } catch (...) {
      }
   }

   void start_on_executor() {
      if (started) {
         return;
      }
      if (stopping) {
         FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP server cannot start while shutdown is in progress");
      }

      const auto address = asio::ip::make_address(config.bind_address);
      auto endpoint = tcp::endpoint{address, config.port};

      acceptor.open(endpoint.protocol());
      acceptor.set_option(asio::socket_base::reuse_address(true));
      acceptor.bind(endpoint);
      acceptor.listen(asio::socket_base::max_listen_connections);
      accept_loop_completion.expires_at((std::chrono::steady_clock::time_point::max)());
      stop_completion.expires_at((std::chrono::steady_clock::time_point::max)());
      accept_loop_completed = false;
      started = true;
      stopped.store(false, std::memory_order_release);

      auto self = shared_from_this();
      auto operation = self->accept_loop();
      asio::co_spawn(acceptor_executor, std::move(operation), [self](std::exception_ptr error) {
         self->complete_accept_loop();
         if (error) {
            try {
               std::rethrow_exception(error);
            } catch (const std::exception&) {
            }
         }
      });
   }

   void stop_on_executor() {
      if (stopped.load(std::memory_order_acquire) || stopping) {
         return;
      }
      auto self = shared_from_this();
      auto operation = self->async_stop_on_executor();
      asio::co_spawn(acceptor_executor, std::move(operation), [self](std::exception_ptr error) {
         if (error) {
            try {
               std::rethrow_exception(error);
            } catch (const std::exception&) {
            }
         }
      });
   }

   void stop_after_runtime_stopped() {
      auto ignored = boost::system::error_code{};
      acceptor.cancel(ignored);
      acceptor.close(ignored);
      cancel_sessions_after_runtime_stopped();
      complete_accept_loop();
      complete_stop();
   }

   awaitable<void> async_stop_on_executor() {
      if (stopped.load(std::memory_order_acquire)) {
         co_return;
      }
      if (stopping) {
         co_await wait_until_stopped();
         co_return;
      }

      stopping = true;
      auto ignored = boost::system::error_code{};
      acceptor.cancel(ignored);
      acceptor.close(ignored);
      started = false;
      try {
         begin_cancel_sessions();
         co_await wait_until_accept_loop_completed();
         co_await async_cancel_sessions();
      } catch (...) {
         complete_stop();
         throw;
      }
      complete_stop();
   }
};

server::server(forge::asio::runtime& runtime, server_config config, server_handler handler)
    : impl_(std::make_shared<impl>(runtime, std::move(config), std::move(handler), nullptr)) {}

server::server(forge::asio::runtime& runtime, server_config config, router router_value)
    : impl_(std::make_shared<impl>(runtime, std::move(config), server_handler{},
                                   std::make_shared<router>(std::move(router_value)))) {}

server::~server() {
   if (impl_ && !impl_->stopped.load(std::memory_order_acquire)) {
      stop();
   }
}

void server::start() {
   struct start_state {
      std::mutex mutex;
      std::condition_variable ready;
      bool done = false;
      std::exception_ptr error;
   };

   auto state = std::make_shared<start_state>();
   auto impl = impl_;
   if (impl->runtime.context().stopped()) {
      FORGE_THROW_EXCEPTION(exceptions::internal, "HTTP server cannot start after runtime stop");
   }
   if (impl->acceptor_executor.running_in_this_thread()) {
      impl->start_on_executor();
      return;
   }
   if (impl->runtime.context().get_executor().running_in_this_thread()) {
      FORGE_THROW_EXCEPTION(exceptions::internal,
                            "HTTP server synchronous start cannot run on a runtime worker; use async_start()");
   }

   asio::dispatch(impl->acceptor_executor, [impl, state] {
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
   if (!impl_) {
      return;
   }

   auto impl = impl_;
   if (impl->runtime.context().stopped()) {
      impl->stop_after_runtime_stopped();
      return;
   }
   if (impl->acceptor_executor.running_in_this_thread()) {
      impl->stop_on_executor();
      return;
   }
   if (impl->runtime.context().get_executor().running_in_this_thread()) {
      asio::post(impl->acceptor_executor, [impl] { impl->stop_on_executor(); });
      return;
   }

   struct stop_state {
      std::mutex mutex;
      std::condition_variable ready;
      bool done = false;
      std::exception_ptr error;
   };

   auto state = std::make_shared<stop_state>();
   auto operation = impl->async_stop_on_executor();
   asio::co_spawn(impl->acceptor_executor, std::move(operation), [impl, state](std::exception_ptr error) {
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
   auto impl = impl_;
   co_await asio::dispatch(impl->acceptor_executor, use_awaitable);
   impl->start_on_executor();
}

boost::asio::awaitable<void> server::async_stop() {
   if (!impl_) {
      co_return;
   }
   auto impl = impl_;
   co_await asio::dispatch(impl->acceptor_executor, use_awaitable);
   co_await impl->async_stop_on_executor();
}

std::uint16_t server::port() const {
   if (!impl_->acceptor.is_open()) {
      return 0;
   }
   return impl_->acceptor.local_endpoint().port();
}

} // namespace forge::net::http
