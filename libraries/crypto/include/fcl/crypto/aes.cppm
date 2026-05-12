module;

export module fcl.crypto.aes;

import fcl.crypto.types;

export namespace fcl::crypto {

[[nodiscard]] aes256_gcm_ciphertext encrypt_aes256_gcm(
   const aes256_gcm_encrypt_request& request);

[[nodiscard]] bytes decrypt_aes256_gcm(
   const aes256_gcm_decrypt_request& request);

[[nodiscard]] aes256_cbc_ciphertext encrypt_aes256_cbc(
   const aes256_cbc_encrypt_request& request);

[[nodiscard]] bytes decrypt_aes256_cbc(
   const aes256_cbc_decrypt_request& request);

} // namespace fcl::crypto
