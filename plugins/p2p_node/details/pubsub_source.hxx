#pragma once

namespace fcl::plugins::p2p_node {

class plugin::pubsub_source_adapter final : public pubsub_source {
 public:
   explicit pubsub_source_adapter(std::shared_ptr<plugin::impl> impl);

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
