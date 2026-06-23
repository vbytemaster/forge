#pragma once

namespace forge::plugins::p2p::node {

class plugin::node_api final : public api {
 public:
   explicit node_api(std::shared_ptr<plugin::impl> impl);

   forge::p2p::peer_id local_peer() const override;
   std::optional<forge::p2p::endpoint> local_endpoint() const override;
   std::vector<forge::p2p::endpoint> local_endpoints() const override;
   info network_info() const override;
   void publish_api(forge::api::binding_plan plan, forge::p2p::protocol_id protocol) override;
   void publish_api(forge::api::binding_plan plan, forge::p2p::protocol_id protocol,
                    forge::transport::api::options options) override;
   void publish_protocol(forge::p2p::protocol_id protocol, forge::p2p::node::protocol_handler handler) override;
   boost::asio::awaitable<forge::transport::api::connection>
   open_api_connection(forge::p2p::peer_id peer, forge::p2p::protocol_id protocol, remote_options options) override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace forge::plugins::p2p::node
