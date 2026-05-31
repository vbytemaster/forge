#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include <boost/asio/awaitable.hpp>

namespace fcl::p2p::direct {

struct connection {
   peer_id peer;
   fcl::transport::session session;
   std::optional<fcl::p2p::endpoint> local_endpoint;
   std::optional<fcl::p2p::endpoint> remote_endpoint;
};

class profile {
 public:
   virtual ~profile() = default;

   [[nodiscard]] virtual bool supports(const fcl::p2p::endpoint& endpoint) const noexcept = 0;
   [[nodiscard]] virtual bool listening() const noexcept = 0;
   [[nodiscard]] virtual std::optional<fcl::p2p::endpoint> local_endpoint() const = 0;

   virtual void listen(fcl::p2p::endpoint endpoint) = 0;
   virtual void stop() = 0;

   virtual boost::asio::awaitable<connection> async_connect(fcl::p2p::endpoint endpoint,
                                                            const node::connect_options& options) = 0;
   virtual boost::asio::awaitable<connection> async_accept() = 0;
};

class registry {
 public:
   registry(fcl::asio::runtime& runtime, const node::options& options);
   ~registry();

   registry(const registry&) = delete;
   registry& operator=(const registry&) = delete;

   [[nodiscard]] bool listening() const noexcept;
   [[nodiscard]] std::optional<fcl::p2p::endpoint> local_endpoint() const;

   void add(std::unique_ptr<profile> value);
   void listen(fcl::p2p::endpoint endpoint);
   void stop();

   boost::asio::awaitable<connection> async_connect(fcl::p2p::endpoint endpoint,
                                                    const node::connect_options& options);
   boost::asio::awaitable<connection> async_accept();

 private:
   struct state;
   std::unique_ptr<state> state_;
};

void register_quic_profile(registry& value, fcl::asio::runtime& runtime, const node::options& options);

} // namespace fcl::p2p::direct
