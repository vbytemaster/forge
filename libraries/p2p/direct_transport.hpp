#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <boost/asio/awaitable.hpp>

namespace fcl::p2p::direct {

struct connection {
   peer_id peer;
   fcl::transport::session session;
   std::optional<fcl::p2p::endpoint> local_endpoint;
   std::optional<fcl::p2p::endpoint> remote_endpoint;
};

struct profile {
   std::function<bool(const fcl::p2p::endpoint&)> supports;
   std::function<bool()> listening;
   std::function<std::vector<fcl::p2p::endpoint>()> local_endpoints;
   std::function<fcl::p2p::endpoint(fcl::p2p::endpoint)> listen;
   std::function<void()> stop;
   std::function<boost::asio::awaitable<connection>(fcl::p2p::endpoint, const node::connect_options&)> async_connect;
   std::function<boost::asio::awaitable<connection>(fcl::p2p::endpoint)> async_accept;
};

class registry {
 public:
   registry(fcl::asio::runtime& runtime, const node::options& options);
   ~registry();

   registry(const registry&) = delete;
   registry& operator=(const registry&) = delete;

   [[nodiscard]] bool listening() const noexcept;
   [[nodiscard]] std::optional<fcl::p2p::endpoint> local_endpoint() const;
   [[nodiscard]] std::vector<fcl::p2p::endpoint> local_endpoints() const;

   void add(profile value);
   [[nodiscard]] fcl::p2p::endpoint listen(fcl::p2p::endpoint endpoint);
   void stop();

   boost::asio::awaitable<connection> async_connect(fcl::p2p::endpoint endpoint,
                                                    const node::connect_options& options);
   boost::asio::awaitable<connection> async_accept(fcl::p2p::endpoint endpoint);

 private:
   struct state;
   std::unique_ptr<state> state_;
};

void register_quic_profile(registry& value, fcl::asio::runtime& runtime, const node::options& options);
void register_tcp_profile(registry& value, fcl::asio::runtime& runtime, const node::options& options);

} // namespace fcl::p2p::direct
