#pragma once

namespace fcl::plugins::p2p_node {

class plugin::node_api final : public api {
 public:
   explicit node_api(std::shared_ptr<plugin::impl> impl);

   fcl::p2p::peer_id local_peer() const override;
   std::optional<fcl::p2p::endpoint> local_endpoint() const override;
   std::vector<fcl::p2p::endpoint> local_endpoints() const override;
   info network_info() const override;
   void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) override;
   void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol,
                    fcl::api::transport::options options) override;
   void publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::node::protocol_handler handler) override;
   boost::asio::awaitable<fcl::api::transport::connection>
   open_api_connection(fcl::p2p::peer_id peer, fcl::p2p::protocol_id protocol, remote_options options) override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace fcl::plugins::p2p_node
