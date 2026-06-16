#pragma once

namespace fcl::plugins::http_server {

class plugin::api_impl final : public api {
 public:
   explicit api_impl(std::shared_ptr<plugin::impl> impl);

   boost::asio::awaitable<void> publish(publication value) override;
   boost::asio::awaitable<void> use(fcl::http::middleware_descriptor descriptor) override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace fcl::plugins::http_server
