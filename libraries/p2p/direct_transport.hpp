#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <boost/asio/awaitable.hpp>

namespace forge::p2p::direct {

struct connection {
   peer_id peer;
   forge::transport::session session;
   std::optional<forge::p2p::endpoint> local_endpoint;
   std::optional<forge::p2p::endpoint> remote_endpoint;
};

struct profile {
   std::function<bool(const forge::p2p::endpoint&)> supports;
   std::function<bool()> listening;
   std::function<std::vector<forge::p2p::endpoint>()> local_endpoints;
   std::function<forge::p2p::endpoint(forge::p2p::endpoint)> listen;
   std::function<void()> stop;
   std::function<boost::asio::awaitable<connection>(forge::p2p::endpoint, const node::connect_options&)> async_connect;
   std::function<boost::asio::awaitable<connection>(forge::p2p::endpoint)> async_accept;
};

class registry {
 public:
   registry(forge::asio::runtime& runtime, const node::options& options);
   ~registry();

   registry(const registry&) = delete;
   registry& operator=(const registry&) = delete;

   [[nodiscard]] bool listening() const noexcept;
   [[nodiscard]] std::optional<forge::p2p::endpoint> local_endpoint() const;
   [[nodiscard]] std::vector<forge::p2p::endpoint> local_endpoints() const;

   void add(profile value);
   [[nodiscard]] forge::p2p::endpoint listen(forge::p2p::endpoint endpoint);
   void stop();

   boost::asio::awaitable<connection> async_connect(forge::p2p::endpoint endpoint,
                                                    const node::connect_options& options);
   boost::asio::awaitable<connection> async_accept(forge::p2p::endpoint endpoint);

 private:
   struct state;
   std::unique_ptr<state> state_;
};

void register_quic_profile(registry& value, forge::asio::runtime& runtime, const node::options& options);
void register_tcp_profile(registry& value, forge::asio::runtime& runtime, const node::options& options);

} // namespace forge::p2p::direct
