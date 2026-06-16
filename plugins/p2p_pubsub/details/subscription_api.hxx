#pragma once

namespace fcl::plugins::p2p_pubsub {

class plugin::subscription_api final : public api {
 public:
   explicit subscription_api(std::shared_ptr<plugin::impl> impl);

   boost::asio::awaitable<message> publish(fcl::p2p::pubsub::topic subject, std::vector<std::uint8_t> data,
                                           publish_options options) override;
   boost::asio::awaitable<subscription> subscribe(fcl::p2p::pubsub::topic subject, handler callback,
                                                  subscribe_options options) override;
   boost::asio::awaitable<void> unsubscribe(subscription value) override;
   [[nodiscard]] std::vector<subscription> subscriptions() const override;
   [[nodiscard]] ::fcl::plugins::p2p_pubsub::snapshot snapshot() const override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace fcl::plugins::p2p_pubsub
