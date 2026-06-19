#pragma once

namespace fcl::plugins::secret_provider {

class plugin::secret_api final : public api {
 public:
   explicit secret_api(std::shared_ptr<impl> state);

   boost::asio::awaitable<snapshot> status(query value) override;
   boost::asio::awaitable<get_result> get_bytes(get_request value) override;
   boost::asio::awaitable<derive_result> derive_hkdf_sha256(derive_request value) override;
   boost::asio::awaitable<aead_encrypt_result> encrypt_aes_gcm(aead_encrypt_request value) override;
   boost::asio::awaitable<aead_decrypt_result> decrypt_aes_gcm(aead_decrypt_request value) override;

 private:
   std::shared_ptr<impl> state_;
};

} // namespace fcl::plugins::secret_provider
