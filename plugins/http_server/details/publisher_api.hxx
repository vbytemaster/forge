#pragma once

namespace fcl::plugins::http_server {

class plugin::publisher_api final : public api {
 public:
   explicit publisher_api(std::shared_ptr<plugin::impl> impl);

   [[nodiscard]] const fcl::api::registry& registry() const override;
   boost::asio::awaitable<void> publish_typed(std::type_index interface_type,
                                              publish_options options,
                                              std::shared_ptr<void> (*factory)(const fcl::api::registry&)) override;
   boost::asio::awaitable<void> use(fcl::http::middleware_descriptor descriptor) override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace fcl::plugins::http_server
