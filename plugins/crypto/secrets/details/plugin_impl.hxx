#pragma once

namespace fcl::plugins::crypto::secrets {

struct plugin::impl {
   struct loaded_secret {
      std::string id;
      secret_kind kind = secret_kind::symmetric_key;
      fcl::crypto::secret_bytes material;
      std::vector<std::string> purposes;
      std::vector<operation> operations;
      bool allow_raw_export = false;
      std::uint64_t max_plaintext_bytes = default_max_plaintext_bytes;
      std::uint64_t max_ciphertext_bytes = default_max_ciphertext_bytes;
      std::uint64_t max_aad_bytes = default_max_aad_bytes;
   };

   [[nodiscard]] snapshot status(query value) const;
   [[nodiscard]] get_result get_bytes(get_request value) const;
   [[nodiscard]] derive_result derive_hkdf_sha256(derive_request value) const;
   [[nodiscard]] aead_encrypt_result encrypt_aes_gcm(aead_encrypt_request value) const;
   [[nodiscard]] aead_decrypt_result decrypt_aes_gcm(aead_decrypt_request value) const;

   std::map<std::string, loaded_secret> secrets;
   bool stopping = false;
};

} // namespace fcl::plugins::crypto::secrets
