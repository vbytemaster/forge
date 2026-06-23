module;

#include <chrono>
#include <coroutine>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

module forge.websocket.connection;

import forge.exceptions;
import forge.websocket.exceptions;

namespace forge::websocket {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_websocket = boost::beast::websocket;
using asio::use_awaitable;

using plain_stream = beast_websocket::stream<beast::tcp_stream>;
using tls_stream = beast_websocket::stream<beast::ssl_stream<beast::tcp_stream>>;
using stream_variant = std::variant<plain_stream, tls_stream>;

struct completion_state {
   explicit completion_state(asio::any_io_executor executor)
       : timer(std::move(executor), (std::chrono::steady_clock::time_point::max)()) {}

   void complete() {
      {
         const auto lock = std::scoped_lock{mutex};
         completed = true;
      }
      timer.cancel();
   }

   void complete_error(std::exception_ptr error_value) {
      {
         const auto lock = std::scoped_lock{mutex};
         error = std::move(error_value);
         completed = true;
      }
      timer.cancel();
   }

   bool is_completed() const {
      const auto lock = std::scoped_lock{mutex};
      return completed;
   }

   void rethrow_if_failed() const {
      const auto lock = std::scoped_lock{mutex};
      if (error) {
         std::rethrow_exception(error);
      }
   }

   asio::steady_timer timer;
   mutable std::mutex mutex;
   std::exception_ptr error;
   bool completed = false;
};

asio::awaitable<void> wait_completion(const std::shared_ptr<completion_state>& completion) {
   while (!completion->is_completed()) {
      auto error = boost::system::error_code{};
      co_await completion->timer.async_wait(asio::redirect_error(use_awaitable, error));
      static_cast<void>(error);
   }
   completion->rethrow_if_failed();
}

struct write_operation {
   std::string message;
   std::shared_ptr<completion_state> completion;
};

} // namespace

struct connection::impl {
   explicit impl(beast::tcp_stream stream_value) : stream(plain_stream{std::move(stream_value)}) {}

   explicit impl(beast::ssl_stream<beast::tcp_stream> stream_value) : stream(tls_stream{std::move(stream_value)}) {}

   [[nodiscard]] asio::any_io_executor executor() {
      return std::visit([](auto& stream_value) -> asio::any_io_executor { return stream_value.get_executor(); },
                        stream);
   }

   void complete_pending_with_error(std::exception_ptr error) {
      while (!writes.empty()) {
         writes.front()->completion->complete_error(error);
         writes.pop_front();
      }
      writing = false;
      refresh_queue_depth();
   }

   void start_write(connection& owner) {
      if (writing || writes.empty() || closing) {
         return;
      }

      writing = true;
      auto operation = writes.front();
      auto self = owner.shared_from_this();
      std::visit(
          [this, self, operation](auto& stream_value) {
             stream_value.async_write(
                 asio::buffer(operation->message),
                 asio::bind_executor(stream_value.get_executor(),
                                     [this, self, operation](boost::system::error_code error, std::size_t bytes) {
                                        static_cast<void>(bytes);
                                        writes.pop_front();
                                        writing = false;
                                        if (error) {
                                           operation->completion->complete_error(
                                               std::make_exception_ptr(boost::system::system_error{error}));
                                           record_failed_write();
                                           complete_pending_with_error(
                                               std::make_exception_ptr(boost::system::system_error{error}));
                                           return;
                                        }

                                        record_sent();
                                        operation->completion->complete();
                                        start_write(*self);
                                     }));
          },
          stream);
   }

   asio::awaitable<void> accept(const boost::beast::http::request<boost::beast::http::string_body>& request_value) {
      co_await std::visit(
          [&request_value](auto& stream_value) -> asio::awaitable<void> {
             stream_value.set_option(beast_websocket::stream_base::timeout::suggested(beast::role_type::server));
             co_await stream_value.async_accept(request_value, use_awaitable);
          },
          stream);
   }

   asio::awaitable<void> handshake(std::string host, std::string target) {
      co_await std::visit(
          [&host, &target](auto& stream_value) -> asio::awaitable<void> {
             stream_value.set_option(beast_websocket::stream_base::timeout::suggested(beast::role_type::client));
             co_await stream_value.async_handshake(host, target, use_awaitable);
          },
          stream);
   }

   asio::awaitable<std::pair<boost::system::error_code, std::size_t>> read_one() {
      co_return co_await std::visit(
          [this](auto& stream_value) -> asio::awaitable<std::pair<boost::system::error_code, std::size_t>> {
             co_return co_await stream_value.async_read(buffer, asio::as_tuple(use_awaitable));
          },
          stream);
   }

   void record_queued_write() {
      const auto lock = std::scoped_lock{metrics_mutex};
      current_metrics.queued_writes = writes.size();
   }

   void refresh_queue_depth() {
      const auto lock = std::scoped_lock{metrics_mutex};
      current_metrics.queued_writes = writes.size();
   }

   void record_sent() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.sent_messages;
      current_metrics.queued_writes = writes.size();
   }

   void record_received() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.received_messages;
   }

   void record_failed_write() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.failed_writes;
      current_metrics.queued_writes = writes.size();
   }

   void record_ping() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.ping_count;
   }

   void record_close() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.close_count;
   }

   void record_handler_failure() {
      const auto lock = std::scoped_lock{metrics_mutex};
      ++current_metrics.handler_failures;
   }

   [[nodiscard]] connection_metrics metrics() const {
      const auto lock = std::scoped_lock{metrics_mutex};
      auto snapshot = current_metrics;
      snapshot.queued_writes = writes.size();
      return snapshot;
   }

   stream_variant stream;
   beast::flat_buffer buffer;
   message_handler on_message;
   close_handler on_close;
   std::deque<std::shared_ptr<write_operation>> writes;
   mutable std::mutex metrics_mutex;
   connection_metrics current_metrics{};
   bool writing = false;
   bool closing = false;
};

connection::connection(beast::tcp_stream stream) : impl_(std::make_unique<impl>(std::move(stream))) {}

connection::connection(beast::ssl_stream<beast::tcp_stream> stream)
    : impl_(std::make_unique<impl>(std::move(stream))) {}

connection::~connection() = default;

connection::ptr connection::create(beast::tcp_stream stream) {
   return connection::ptr{new connection(std::move(stream))};
}

connection::ptr connection::create(beast::ssl_stream<beast::tcp_stream> stream) {
   return connection::ptr{new connection(std::move(stream))};
}

void connection::on_message(message_handler handler) {
   asio::post(impl_->executor(),
              [impl = impl_.get(), handler = std::move(handler)]() mutable { impl->on_message = std::move(handler); });
}

void connection::on_close(close_handler handler) {
   asio::post(impl_->executor(),
              [impl = impl_.get(), handler = std::move(handler)]() mutable { impl->on_close = std::move(handler); });
}

boost::asio::awaitable<void> connection::send(std::string message) {
   auto completion = std::make_shared<completion_state>(impl_->executor());
   asio::post(impl_->executor(), [self = shared_from_this(), message = std::move(message), completion]() mutable {
      if (self->impl_->closing) {
         completion->complete_error(
             std::make_exception_ptr(exceptions::closed{"websocket connection is closing"}));
         return;
      }
      self->impl_->writes.push_back(std::make_shared<write_operation>(write_operation{
          .message = std::move(message),
          .completion = std::move(completion),
      }));
      self->impl_->record_queued_write();
      self->impl_->start_write(*self);
   });
   co_await wait_completion(completion);
}

boost::asio::awaitable<void> connection::close() {
   auto completion = std::make_shared<completion_state>(impl_->executor());
   asio::post(impl_->executor(), [self = shared_from_this(), completion]() {
      if (self->impl_->closing) {
         completion->complete();
         return;
      }

      self->impl_->closing = true;
      std::visit(
          [self, completion](auto& stream_value) {
             stream_value.async_close(
                 beast_websocket::close_code::normal,
                 asio::bind_executor(stream_value.get_executor(), [self, completion](boost::system::error_code error) {
                    if (error) {
                       completion->complete_error(std::make_exception_ptr(boost::system::system_error{error}));
                       return;
                    }
                    self->impl_->record_close();
                    completion->complete();
                 }));
          },
          self->impl_->stream);
   });
   co_await wait_completion(completion);
}

boost::asio::awaitable<void> connection::ping(std::string payload) {
   auto completion = std::make_shared<completion_state>(impl_->executor());
   asio::post(impl_->executor(), [self = shared_from_this(), payload = std::move(payload), completion]() {
      std::visit(
          [self, payload, completion](auto& stream_value) {
             stream_value.async_ping(
                 beast_websocket::ping_data{payload},
                 asio::bind_executor(stream_value.get_executor(), [self, completion](boost::system::error_code error) {
                    if (error) {
                       completion->complete_error(std::make_exception_ptr(boost::system::system_error{error}));
                       return;
                    }
                    self->impl_->record_ping();
                    completion->complete();
                 }));
          },
          self->impl_->stream);
   });
   co_await wait_completion(completion);
}

boost::asio::awaitable<void>
connection::accept(const boost::beast::http::request<boost::beast::http::string_body>& request_value) {
   co_await impl_->accept(request_value);
}

boost::asio::awaitable<void> connection::handshake(std::string host, std::string target) {
   co_await impl_->handshake(std::move(host), std::move(target));
}

void connection::start_read_loop() {
   auto self = shared_from_this();
   asio::co_spawn(
       impl_->executor(),
       [self]() -> asio::awaitable<void> {
          for (;;) {
             auto [error, bytes] = co_await self->impl_->read_one();
             static_cast<void>(bytes);
             if (error) {
                if (self->impl_->on_close) {
                   self->impl_->on_close(*self);
                }
                co_return;
             }

             auto message = beast::buffers_to_string(self->impl_->buffer.data());
             self->impl_->buffer.consume(self->impl_->buffer.size());
             self->impl_->record_received();
             if (self->impl_->on_message) {
                try {
                   co_await self->impl_->on_message(*self, std::move(message));
                } catch (const forge::exceptions::base&) {
                   self->impl_->record_handler_failure();
                   if (self->impl_->on_close) {
                      self->impl_->on_close(*self);
                   }
                   co_return;
                } catch (const std::exception&) {
                   self->impl_->record_handler_failure();
                   if (self->impl_->on_close) {
                      self->impl_->on_close(*self);
                   }
                   co_return;
                } catch (...) {
                   self->impl_->record_handler_failure();
                   if (self->impl_->on_close) {
                      self->impl_->on_close(*self);
                   }
                   co_return;
                }
             }
          }
       },
       [self](std::exception_ptr error) {
          if (error) {
             self->impl_->record_handler_failure();
          }
       });
}

connection_metrics connection::metrics() const {
   return impl_->metrics();
}

} // namespace forge::websocket
