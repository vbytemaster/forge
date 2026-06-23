#pragma once

namespace forge::plugins::crypto::signer {

class plugin::signer_api final : public api {
 public:
   explicit signer_api(std::shared_ptr<impl> state);

   boost::asio::awaitable<response> sign(request value) override;

 private:
   std::shared_ptr<impl> state_;
};

} // namespace forge::plugins::crypto::signer
