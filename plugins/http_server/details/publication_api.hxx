#pragma once

namespace fcl::plugins::http_server {

class plugin::publication_api final : public api {
 public:
   explicit publication_api(std::shared_ptr<plugin::impl> impl);

   boost::asio::awaitable<void> publish(publication value) override;
   boost::asio::awaitable<void> use(fcl::http::middleware_descriptor descriptor) override;

 private:
   std::shared_ptr<plugin::impl> impl_;
};

} // namespace fcl::plugins::http_server
