module;

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <boost/asio/awaitable.hpp>

export module fcl.http.server;

import fcl.asio.runtime;
import fcl.http.middleware;
import fcl.http.route_context;
import fcl.http.router;
import fcl.http.types;

export namespace fcl::http {

struct server_config {
   std::string bind_address = "127.0.0.1";
   std::uint16_t port = 0;
};

using server_handler = std::function<boost::asio::awaitable<response>(route_context&)>;

class server {
 public:
   server(fcl::asio::runtime& runtime, server_config config, server_handler handler);
   server(fcl::asio::runtime& runtime, server_config config, router router_value);
   ~server();

   server(const server&) = delete;
   server& operator=(const server&) = delete;

   void start();
   void stop();

   [[nodiscard]] std::uint16_t port() const;

 private:
   struct impl;

   std::unique_ptr<impl> impl_;
};

} // namespace fcl::http
