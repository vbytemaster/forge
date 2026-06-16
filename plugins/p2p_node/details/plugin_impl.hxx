#pragma once

#include "config.hxx"

namespace fcl::plugins::p2p_node {

[[nodiscard]] fcl::p2p::peer_id default_test_peer();

struct plugin::impl : public std::enable_shared_from_this<plugin::impl> {
   fcl::p2p::node::options options{
      .explicit_peer_id = default_test_peer(),
      .allow_insecure_test_mode = false,
   };
   fcl::api::transport::options api_options{
      .codec = fcl::api::codec_id{.value = "fcl.raw"},
      .max_inflight = 64,
   };
   parsed_policy policy{};
   std::vector<fcl::p2p::endpoint> listen;
   std::vector<fcl::p2p::endpoint> bootstrap;
   std::vector<std::pair<fcl::p2p::protocol_id, fcl::p2p::node::protocol_handler>> routes;
   fcl::p2p::pubsub::options pubsub_options{};
   fcl::p2p::node* raw = nullptr;
   std::unique_ptr<fcl::p2p::node> node;
   fcl::asio::runtime* runtime = nullptr;
   bool pubsub_requested = false;
   bool started = false;
   bool stopping = false;

   [[nodiscard]] fcl::p2p::node& ensure_node();
   [[nodiscard]] fcl::p2p::node& require_node();
   [[nodiscard]] const fcl::p2p::node& require_node() const;
   void add_route(fcl::p2p::protocol_id protocol, fcl::p2p::node::protocol_handler handler);
   [[nodiscard]] fcl::p2p::node::open_options open_options_for(remote_options value) const;
   [[nodiscard]] fcl::api::transport::options api_options_for(const remote_options& value) const;
};

} // namespace fcl::plugins::p2p_node
