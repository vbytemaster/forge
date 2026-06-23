module;

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

export module forge.websocket.client;

import forge.asio.runtime;
import forge.websocket.connection;

export namespace forge::websocket {

namespace detail {
class client_impl;
} // namespace detail

struct client_options {
   std::chrono::milliseconds timeout{30'000};
   bool verify_peer = true;
};

struct client_endpoint {
   std::string host;
   std::string port;
   std::string base_path = "/";
   bool tls = false;

   [[nodiscard]] bool secure() const {
      return tls;
   }
   [[nodiscard]] std::string make_target(std::string_view path) const {
      auto normalized = std::string{path};
      if (normalized.empty()) {
         normalized = "/";
      } else if (normalized.front() != '/') {
         normalized.insert(normalized.begin(), '/');
      }

      auto target = base_path.empty() ? std::string{"/"} : base_path;
      if (target.back() == '/' && normalized.front() == '/') {
         target.pop_back();
      }
      if (target.empty() || target == "/") {
         return normalized;
      }
      return target + normalized;
   }
};

class client {
 public:
   client(forge::asio::runtime& runtime, client_endpoint endpoint);

   template <typename Endpoint>
   client(forge::asio::runtime& runtime, const Endpoint& endpoint)
       : client(runtime, client_endpoint{
                             .host = endpoint.host,
                             .port = endpoint.port,
                             .base_path = endpoint.base_path,
                             .tls = endpoint.secure(),
                         }) {}

   ~client();

   client(const client&) = delete;
   client& operator=(const client&) = delete;

   boost::asio::awaitable<connection::ptr> async_connect(std::string_view path, client_options options = {});
   connection::ptr connect(std::string_view path, client_options options = {});

 private:
   std::unique_ptr<detail::client_impl> impl_;
};

} // namespace forge::websocket
