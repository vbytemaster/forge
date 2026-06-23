module;

#include <coroutine>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl.hpp>

module forge.websocket.client;

import forge.asio.blocking;
import forge.asio.runtime;
import forge.websocket.connection;
import forge.websocket.exceptions;

namespace forge::websocket {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
using asio::awaitable;
using asio::use_awaitable;

} // namespace

namespace detail {

class client_impl {
 public:
   client_impl(forge::asio::runtime& runtime_value, client_endpoint endpoint_value)
       : runtime(runtime_value), endpoint(std::move(endpoint_value)), strand(asio::make_strand(runtime.context())),
         resolver(strand), ssl_context(asio::ssl::context::tls_client) {
      ssl_context.set_default_verify_paths();
      ssl_context.set_verify_mode(asio::ssl::verify_peer);
   }

   awaitable<connection::ptr> connect_coro(std::string path, client_options options) {
      auto results = co_await resolver.async_resolve(endpoint.host, endpoint.port, use_awaitable);
      if (endpoint.secure()) {
         auto stream = beast::ssl_stream<beast::tcp_stream>{strand, ssl_context};
         if (!SSL_set_tlsext_host_name(stream.native_handle(), endpoint.host.c_str())) {
            throw exceptions::internal{"failed to configure TLS host name"};
         }
         if (options.verify_peer) {
            stream.set_verify_mode(asio::ssl::verify_peer);
            stream.set_verify_callback(asio::ssl::host_name_verification(endpoint.host));
         } else {
            stream.set_verify_mode(asio::ssl::verify_none);
         }

         beast::get_lowest_layer(stream).expires_after(options.timeout);
         co_await beast::get_lowest_layer(stream).async_connect(results, use_awaitable);
         co_await stream.async_handshake(asio::ssl::stream_base::client, use_awaitable);

         auto connection_value = connection::create(std::move(stream));
         co_await connection_value->handshake(endpoint.host, endpoint.make_target(path));
         connection_value->start_read_loop();
         co_return connection_value;
      }

      auto stream = beast::tcp_stream{strand};
      stream.expires_after(options.timeout);
      co_await stream.async_connect(results, use_awaitable);

      auto connection_value = connection::create(std::move(stream));
      co_await connection_value->handshake(endpoint.host, endpoint.make_target(path));
      connection_value->start_read_loop();
      co_return connection_value;
   }

   forge::asio::runtime& runtime;
   client_endpoint endpoint;
   asio::strand<asio::io_context::executor_type> strand;
   tcp::resolver resolver;
   asio::ssl::context ssl_context;
};

} // namespace detail

client::client(forge::asio::runtime& runtime, client_endpoint endpoint)
    : impl_(std::make_unique<detail::client_impl>(runtime, std::move(endpoint))) {}

client::~client() = default;

boost::asio::awaitable<connection::ptr> client::async_connect(std::string_view path, client_options options) {
   auto path_string = std::string{path};
   co_return co_await impl_->connect_coro(std::move(path_string), options);
}

connection::ptr client::connect(std::string_view path, client_options options) {
   return forge::asio::blocking::run(impl_->runtime, async_connect(path, options));
}

} // namespace forge::websocket
