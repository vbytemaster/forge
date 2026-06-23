#pragma once

#include "config.hxx"

namespace forge::plugins::p2p::node {

struct plugin::impl : public std::enable_shared_from_this<plugin::impl> {
   forge::p2p::node::options options{
      .allow_insecure_test_mode = false,
   };
   forge::transport::api::options api_options{
      .codec = forge::api::codec_id{.value = "forge.raw"},
      .max_inflight = 64,
   };
   parsed_policy policy{};
   std::vector<forge::p2p::endpoint> listen;
   std::vector<forge::p2p::endpoint> bootstrap;
   std::vector<std::pair<forge::p2p::protocol_id, forge::p2p::node::protocol_handler>> routes;
   forge::p2p::pubsub::options pubsub_options{};
   forge::p2p::node* raw = nullptr;
   std::unique_ptr<forge::p2p::node> node;
   forge::asio::runtime* runtime = nullptr;
   bool pubsub_requested = false;
   bool started = false;
   bool stopping = false;

   [[nodiscard]] forge::p2p::node& ensure_node();
   [[nodiscard]] forge::p2p::node& require_node();
   [[nodiscard]] const forge::p2p::node& require_node() const;
   void add_route(forge::p2p::protocol_id protocol, forge::p2p::node::protocol_handler handler);
   [[nodiscard]] forge::p2p::node::open_options open_options_for(remote_options value) const;
   [[nodiscard]] forge::transport::api::options api_options_for(const remote_options& value) const;
};

} // namespace forge::plugins::p2p::node
