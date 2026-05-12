module;

#include <openssl/evp.h>

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>

module fcl.crypto.aes;

namespace fcl::crypto {
namespace {

using ctx_ptr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

[[nodiscard]] ctx_ptr make_context()
{
   auto context = ctx_ptr{EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free};
   if (!context) {
      throw error{error_kind::backend_error, "failed to allocate AES context"};
   }
   return context;
}

[[nodiscard]] int checked_update_size(std::size_t size, const char* label)
{
   if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      throw error{error_kind::invalid_options, std::string(label) + " is too large"};
   }
   return static_cast<int>(size);
}

void require_size(const bytes& value, std::size_t expected, const char* message, error_kind kind)
{
   if (value.size() != expected) {
      throw error{kind, message};
   }
}

void require_gcm_nonce(const bytes& nonce)
{
   require_size(nonce, aes_gcm_nonce_size, "AES-256-GCM requires 12-byte nonce", error_kind::invalid_nonce);
}

void require_gcm_tag(const bytes& tag)
{
   require_size(tag, aes_gcm_tag_size, "AES-256-GCM requires 16-byte tag", error_kind::invalid_tag);
}

void require_cbc_iv(const bytes& iv)
{
   require_size(iv, aes_cbc_iv_size, "AES-256-CBC requires 16-byte IV", error_kind::invalid_nonce);
}

} // namespace

aes256_gcm_ciphertext encrypt_aes256_gcm(const aes256_gcm_encrypt_request& request)
{
   require_gcm_nonce(request.nonce);

   auto context = make_context();
   auto out = bytes(request.plaintext.size());
   auto out_size = int{};
   auto total_size = int{};
   auto tag = bytes(aes_gcm_tag_size);
   auto final_out = std::array<std::uint8_t, aes_gcm_tag_size>{};

   if (EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
       EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(request.nonce.size()), nullptr) != 1 ||
       EVP_EncryptInit_ex(context.get(), nullptr, nullptr, request.key.bytes.data(), request.nonce.data()) != 1) {
      throw error{error_kind::backend_error, "failed to initialize AES-GCM encryption"};
   }
   if (!request.aad.empty() &&
       EVP_EncryptUpdate(
          context.get(),
          nullptr,
          &out_size,
          request.aad.data(),
          checked_update_size(request.aad.size(), "AES-GCM AAD")) != 1) {
      throw error{error_kind::backend_error, "failed to apply AES-GCM AAD"};
   }
   out_size = 0;
   if (!request.plaintext.empty() &&
       EVP_EncryptUpdate(
          context.get(),
          out.data(),
          &out_size,
          request.plaintext.data(),
          checked_update_size(request.plaintext.size(), "AES-GCM plaintext")) != 1) {
      throw error{error_kind::backend_error, "failed to encrypt AES-GCM payload"};
   }
   total_size = out_size;
   if (EVP_EncryptFinal_ex(context.get(), final_out.data(), &out_size) != 1) {
      throw error{error_kind::backend_error, "failed to finalize AES-GCM encryption"};
   }
   total_size += out_size;
   out.resize(static_cast<std::size_t>(total_size));
   if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) != 1) {
      throw error{error_kind::backend_error, "failed to read AES-GCM tag"};
   }

   return aes256_gcm_ciphertext{
      .nonce = request.nonce,
      .tag = std::move(tag),
      .ciphertext = std::move(out),
   };
}

bytes decrypt_aes256_gcm(const aes256_gcm_decrypt_request& request)
{
   require_gcm_nonce(request.encrypted.nonce);
   require_gcm_tag(request.encrypted.tag);

   auto context = make_context();
   auto out = bytes(request.encrypted.ciphertext.size());
   auto out_size = int{};
   auto total_size = int{};
   auto final_out = std::array<std::uint8_t, aes_gcm_tag_size>{};

   if (EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
       EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(request.encrypted.nonce.size()), nullptr) != 1 ||
       EVP_DecryptInit_ex(context.get(), nullptr, nullptr, request.key.bytes.data(), request.encrypted.nonce.data()) != 1) {
      throw error{error_kind::backend_error, "failed to initialize AES-GCM decryption"};
   }
   if (!request.aad.empty() &&
       EVP_DecryptUpdate(
          context.get(),
          nullptr,
          &out_size,
          request.aad.data(),
          checked_update_size(request.aad.size(), "AES-GCM AAD")) != 1) {
      throw error{error_kind::backend_error, "failed to apply AES-GCM AAD"};
   }
   out_size = 0;
   if (!request.encrypted.ciphertext.empty() &&
       EVP_DecryptUpdate(
          context.get(),
          out.data(),
          &out_size,
          request.encrypted.ciphertext.data(),
          checked_update_size(request.encrypted.ciphertext.size(), "AES-GCM ciphertext")) != 1) {
      throw error{error_kind::backend_error, "failed to decrypt AES-GCM payload"};
   }
   total_size = out_size;
   if (EVP_CIPHER_CTX_ctrl(
          context.get(),
          EVP_CTRL_GCM_SET_TAG,
          static_cast<int>(request.encrypted.tag.size()),
          const_cast<std::uint8_t*>(request.encrypted.tag.data())) != 1 ||
       EVP_DecryptFinal_ex(context.get(), final_out.data(), &out_size) != 1) {
      throw error{error_kind::authentication_failed, "AES-GCM authentication failed"};
   }
   total_size += out_size;
   out.resize(static_cast<std::size_t>(total_size));
   return out;
}

aes256_cbc_ciphertext encrypt_aes256_cbc(const aes256_cbc_encrypt_request& request)
{
   require_cbc_iv(request.iv);

   auto context = make_context();
   auto out = bytes(request.plaintext.size() + aes_cbc_iv_size);
   auto out_size = int{};
   auto total_size = int{};

   if (EVP_EncryptInit_ex(context.get(), EVP_aes_256_cbc(), nullptr, request.key.bytes.data(), request.iv.data()) != 1) {
      throw error{error_kind::backend_error, "failed to initialize AES-CBC encryption"};
   }
   if (!request.plaintext.empty() &&
       EVP_EncryptUpdate(
          context.get(),
          out.data(),
          &out_size,
          request.plaintext.data(),
          checked_update_size(request.plaintext.size(), "AES-CBC plaintext")) != 1) {
      throw error{error_kind::backend_error, "failed to encrypt AES-CBC payload"};
   }
   total_size = out_size;
   if (EVP_EncryptFinal_ex(context.get(), out.data() + total_size, &out_size) != 1) {
      throw error{error_kind::backend_error, "failed to finalize AES-CBC encryption"};
   }
   total_size += out_size;
   out.resize(static_cast<std::size_t>(total_size));

   return aes256_cbc_ciphertext{
      .iv = request.iv,
      .ciphertext = std::move(out),
   };
}

bytes decrypt_aes256_cbc(const aes256_cbc_decrypt_request& request)
{
   require_cbc_iv(request.encrypted.iv);

   auto context = make_context();
   auto out = bytes(request.encrypted.ciphertext.size());
   auto out_size = int{};
   auto total_size = int{};

   if (EVP_DecryptInit_ex(context.get(), EVP_aes_256_cbc(), nullptr, request.key.bytes.data(), request.encrypted.iv.data()) != 1) {
      throw error{error_kind::backend_error, "failed to initialize AES-CBC decryption"};
   }
   if (!request.encrypted.ciphertext.empty() &&
       EVP_DecryptUpdate(
          context.get(),
          out.data(),
          &out_size,
          request.encrypted.ciphertext.data(),
          checked_update_size(request.encrypted.ciphertext.size(), "AES-CBC ciphertext")) != 1) {
      throw error{error_kind::backend_error, "failed to decrypt AES-CBC payload"};
   }
   total_size = out_size;
   if (EVP_DecryptFinal_ex(context.get(), out.data() + total_size, &out_size) != 1) {
      throw error{error_kind::authentication_failed, "AES-CBC decryption failed"};
   }
   total_size += out_size;
   out.resize(static_cast<std::size_t>(total_size));
   return out;
}

} // namespace fcl::crypto
