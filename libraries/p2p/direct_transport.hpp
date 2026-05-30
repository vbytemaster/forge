#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include <boost/asio/awaitable.hpp>

namespace fcl::p2p::direct {

struct dial_result {
   peer_id peer;
   fcl::transport::session session;
};

struct accepted_connection {
   peer_id peer;
   fcl::transport::session session;
};

class driver {
 public:
   driver(fcl::asio::runtime& runtime, const node::options& options);
   ~driver();

   driver(const driver&) = delete;
   driver& operator=(const driver&) = delete;

   [[nodiscard]] bool listening() const noexcept;
   [[nodiscard]] std::optional<fcl::p2p::endpoint> local_endpoint() const;

   void listen(fcl::p2p::endpoint endpoint);
   void stop();

   boost::asio::awaitable<dial_result> async_connect(fcl::p2p::endpoint endpoint,
                                                     const node::connect_options& options);
   boost::asio::awaitable<accepted_connection> async_accept();

 private:
   struct state;
   std::unique_ptr<state> state_;
};

} // namespace fcl::p2p::direct
