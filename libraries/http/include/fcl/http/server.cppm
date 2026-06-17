module;

#include <cstdint>
#include <chrono>
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
   std::uint64_t max_request_body_bytes = 16 * 1024 * 1024;
   std::uint64_t max_header_bytes = 64 * 1024;
   std::chrono::milliseconds read_timeout{30'000};
   std::chrono::milliseconds idle_timeout{120'000};
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
   boost::asio::awaitable<void> async_start();
   boost::asio::awaitable<void> async_stop();

   [[nodiscard]] std::uint16_t port() const;

 private:
   struct impl;

   std::shared_ptr<impl> impl_;
};

} // namespace fcl::http
