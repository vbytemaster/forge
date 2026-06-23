#pragma once

namespace forge::plugins::http::server {

class plugin::publisher_api final : public api {
 public:
   explicit publisher_api(std::shared_ptr<plugin::impl> impl);

   [[nodiscard]] const forge::api::registry& registry() const override;
   boost::asio::awaitable<void> publish(std::unique_ptr<binding_spec> binding,
                                        publish_options options) override;
   boost::asio::awaitable<void> use(middleware_descriptor descriptor) override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace forge::plugins::http::server
