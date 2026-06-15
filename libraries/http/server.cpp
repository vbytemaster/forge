module;

#include <coroutine>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

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
import fcl.http.route_context;
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

class server_session : public std::enable_shared_from_this<server_session> {
 public:
   server_session(fcl::asio::runtime& runtime, beast::tcp_stream stream, server_handler handler,
                  std::shared_ptr<router> router_value)
       : runtime_{runtime}, stream_(std::move(stream)), handler_(std::move(handler)), router_(std::move(router_value)) {}

   awaitable<void> run() {
      auto self = shared_from_this();
      static_cast<void>(self);

      for (;;) {
         buffer_.consume(buffer_.size());
         auto request_value = request{};
         auto [read_error, bytes] =
             co_await beast_http::async_read(stream_, buffer_, request_value, asio::as_tuple(use_awaitable));
         static_cast<void>(bytes);

         if (read_error == asio::error::eof) {
            co_return;
         }
         if (read_error) {
            throw boost::system::system_error{read_error};
         }

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
            auto [write_error, written] =
                co_await beast_http::async_write(stream_, response_value, asio::as_tuple(use_awaitable));
            static_cast<void>(written);
            if (write_error) {
               throw boost::system::system_error{write_error};
            }
            break;
         }
         auto& context = *context_storage;
         if (beast_websocket::is_upgrade(request_value)) {
            if (co_await try_upgrade(request_value, context)) {
               co_return;
            }
         }

         auto response_value = co_await handle_http(context);
         response_value.version(request_value.version());
         response_value.keep_alive(request_value.keep_alive());

         auto [write_error, written] =
             co_await beast_http::async_write(stream_, response_value, asio::as_tuple(use_awaitable));
         static_cast<void>(written);
         if (write_error) {
            throw boost::system::system_error{write_error};
         }
         if (!response_value.keep_alive()) {
            break;
         }
      }

      auto ignored = boost::system::error_code{};
      stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
   }

 private:
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

   fcl::asio::runtime& runtime_;
   beast::tcp_stream stream_;
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

         auto client =
             std::make_shared<detail::server_session>(runtime, beast::tcp_stream{std::move(socket)}, handler, router_value);
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
   bool started = false;
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
   if (impl_->started) {
      return;
   }

   asio::dispatch(impl_->acceptor.get_executor(), [impl = impl_.get()] {
      const auto address = asio::ip::make_address(impl->config.bind_address);
      auto endpoint = tcp::endpoint{address, impl->config.port};

      impl->acceptor.open(endpoint.protocol());
      impl->acceptor.set_option(asio::socket_base::reuse_address(true));
      impl->acceptor.bind(endpoint);
      impl->acceptor.listen(asio::socket_base::max_listen_connections);
      impl->started = true;

      asio::co_spawn(impl->acceptor.get_executor(), impl->accept_loop(), [](std::exception_ptr error) {
         if (error) {
            try {
               std::rethrow_exception(error);
            } catch (const std::exception&) {
            }
         }
      });
   });
}

void server::stop() {
   if (!impl_ || !impl_->started) {
      return;
   }

   asio::dispatch(impl_->acceptor.get_executor(), [impl = impl_.get()] {
      auto ignored = boost::system::error_code{};
      impl->acceptor.cancel(ignored);
      impl->acceptor.close(ignored);
      impl->started = false;
   });
}

std::uint16_t server::port() const {
   if (!impl_->acceptor.is_open()) {
      return 0;
   }
   return impl_->acceptor.local_endpoint().port();
}

} // namespace fcl::http
