#pragma once

namespace fcl::plugins::signature_provider {

class plugin::api_impl final : public api {
 public:
   explicit api_impl(std::shared_ptr<impl> state);

   boost::asio::awaitable<response> sign(request value) override;

 private:
   std::shared_ptr<impl> state_;
};

} // namespace fcl::plugins::signature_provider
