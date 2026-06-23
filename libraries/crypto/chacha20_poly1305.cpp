module;

#include <forge/exceptions/macros.hpp>

#include <openssl/evp.h>

#include <memory>
#include <span>

module forge.crypto.chacha20_poly1305;

namespace forge::crypto::chacha20_poly1305 {
namespace {

struct cipher_ctx_deleter {
   void operator()(EVP_CIPHER_CTX* value) const noexcept {
      EVP_CIPHER_CTX_free(value);
   }
};

using cipher_ctx_ptr = std::unique_ptr<EVP_CIPHER_CTX, cipher_ctx_deleter>;

[[noreturn]] void fail(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::backend_error, std::move(message));
}

} // namespace

bytes encrypt(const key& key, const nonce& nonce, std::span<const std::uint8_t> associated_data,
              std::span<const std::uint8_t> plaintext) {
   auto context = cipher_ctx_ptr{EVP_CIPHER_CTX_new()};
   if (context == nullptr ||
       EVP_EncryptInit_ex(context.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1 ||
       EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_IVLEN, nonce.size(), nullptr) != 1 ||
       EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
      fail("failed to initialize ChaCha20-Poly1305 encryptor");
   }

   auto len = int{};
   if (!associated_data.empty() &&
       EVP_EncryptUpdate(context.get(), nullptr, &len, associated_data.data(),
                         static_cast<int>(associated_data.size())) != 1) {
      fail("failed to authenticate ChaCha20-Poly1305 associated data");
   }

   auto out = bytes(plaintext.size() + 16U);
   auto written = 0;
   if (!plaintext.empty() &&
       EVP_EncryptUpdate(context.get(), out.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
      fail("failed to encrypt ChaCha20-Poly1305 plaintext");
   }
   written += len;
   if (EVP_EncryptFinal_ex(context.get(), out.data() + written, &len) != 1) {
      fail("failed to finalize ChaCha20-Poly1305 encryption");
   }
   written += len;
   if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_GET_TAG, 16, out.data() + written) != 1) {
      fail("failed to read ChaCha20-Poly1305 tag");
   }
   out.resize(static_cast<std::size_t>(written) + 16U);
   return out;
}

bytes decrypt(const key& key, const nonce& nonce, std::span<const std::uint8_t> associated_data,
              std::span<const std::uint8_t> ciphertext_and_tag) {
   if (ciphertext_and_tag.size() < 16U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_tag, "ChaCha20-Poly1305 ciphertext is missing tag");
   }
   auto context = cipher_ctx_ptr{EVP_CIPHER_CTX_new()};
   if (context == nullptr ||
       EVP_DecryptInit_ex(context.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1 ||
       EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_IVLEN, nonce.size(), nullptr) != 1 ||
       EVP_DecryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
      fail("failed to initialize ChaCha20-Poly1305 decryptor");
   }

   auto len = int{};
   if (!associated_data.empty() &&
       EVP_DecryptUpdate(context.get(), nullptr, &len, associated_data.data(),
                         static_cast<int>(associated_data.size())) != 1) {
      fail("failed to authenticate ChaCha20-Poly1305 associated data");
   }

   const auto payload_size = ciphertext_and_tag.size() - 16U;
   auto out = bytes(payload_size);
   auto written = 0;
   if (payload_size != 0 &&
       EVP_DecryptUpdate(context.get(), out.data(), &len, ciphertext_and_tag.data(), static_cast<int>(payload_size)) !=
          1) {
      fail("failed to decrypt ChaCha20-Poly1305 ciphertext");
   }
   written += len;
   if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_TAG, 16,
                           const_cast<std::uint8_t*>(ciphertext_and_tag.data() + payload_size)) != 1) {
      fail("failed to set ChaCha20-Poly1305 tag");
   }
   if (EVP_DecryptFinal_ex(context.get(), out.data() + written, &len) != 1) {
      FORGE_THROW_EXCEPTION(exceptions::authentication_failed, "ChaCha20-Poly1305 authentication failed");
   }
   written += len;
   out.resize(static_cast<std::size_t>(written));
   return out;
}

} // namespace forge::crypto::chacha20_poly1305
