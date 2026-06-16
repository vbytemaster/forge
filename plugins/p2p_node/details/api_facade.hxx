#pragma once

namespace fcl::plugins::p2p_node {

class plugin::api_impl final : public api {
 public:
   explicit api_impl(std::shared_ptr<plugin::impl> impl);

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

class plugin::diagnostics_source_impl final : public diagnostics_source {
 public:
   explicit diagnostics_source_impl(std::shared_ptr<plugin::impl> impl);

   fcl::p2p::diagnostics::snapshot snapshot(fcl::p2p::diagnostics::options options) const override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

class plugin::pubsub_source_impl final : public pubsub_source {
 public:
   explicit pubsub_source_impl(std::shared_ptr<plugin::impl> impl);

   void enable(fcl::p2p::pubsub::options options) override;
   fcl::p2p::peer_id local_peer() const override;
   boost::asio::awaitable<fcl::p2p::pubsub::message>
   async_publish_message(fcl::p2p::pubsub::topic subject, std::vector<std::uint8_t> data,
                         fcl::p2p::pubsub::publish_options options) override;
   boost::asio::awaitable<fcl::p2p::pubsub::subscription>
   async_join_topic(fcl::p2p::pubsub::topic subject, fcl::p2p::pubsub::handler handler) override;
   boost::asio::awaitable<void> async_leave_topic(fcl::p2p::pubsub::topic subject) override;
   fcl::p2p::pubsub::snapshot snapshot() const override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace fcl::plugins::p2p_node
