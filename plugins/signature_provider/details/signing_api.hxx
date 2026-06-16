#pragma once

namespace fcl::plugins::signature_provider {

class plugin::signing_api final : public api {
 public:
   explicit signing_api(std::shared_ptr<impl> state);

   boost::asio::awaitable<response> sign(request value) override;

 private:
   std::shared_ptr<impl> state_;
};

} // namespace fcl::plugins::signature_provider
