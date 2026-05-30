module;

#include <fcl/exception/macros.hpp>

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>

module fcl.crypto.aes;

import fcl.crypto.random;

namespace fcl::crypto {
namespace {

using ctx_ptr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

[[nodiscard]] ctx_ptr make_context() {
   auto context = ctx_ptr{EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free};
   if (!context) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to allocate AES context");
   }
   return context;
}

[[nodiscard]] int checked_update_size(std::size_t size, const char* label) {
   if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      FCL_THROW_EXCEPTION(aes::exceptions::invalid_options, std::string(label) + " is too large");
   }
   return static_cast<int>(size);
}

void require_size(std::size_t size, std::size_t expected, const char* message, aes::exceptions::code kind) {
   if (size != expected) {
      FCL_THROW_CODE(kind, message);
   }
}

void require_gcm_nonce(std::span<const std::uint8_t> nonce) {
   require_size(nonce.size(), aes_gcm_nonce_size, "AES-256-GCM requires 12-byte nonce",
                aes::exceptions::code::invalid_nonce);
}

void require_gcm_tag(std::span<const std::uint8_t> tag) {
   require_size(tag.size(), aes_gcm_tag_size, "AES-256-GCM requires 16-byte tag",
                aes::exceptions::code::invalid_tag);
}

void require_cbc_iv(std::span<const std::uint8_t> iv) {
   require_size(iv.size(), aes_cbc_iv_size, "AES-256-CBC requires 16-byte IV",
                aes::exceptions::code::invalid_nonce);
}

void require_sink(const aes_byte_sink& sink) {
   if (!sink) {
      FCL_THROW_EXCEPTION(aes::exceptions::invalid_options, "AES streaming requires output sink");
   }
}

void emit_chunk(const aes_byte_sink& sink, bytes& chunk, int size) {
   if (size > 0) {
      sink(std::span<const std::uint8_t>{chunk.data(), static_cast<std::size_t>(size)});
   }
}

} // namespace

struct aes256_gcm_encoder::impl {
   ctx_ptr context = make_context();
   bytes nonce;
   bytes aad;
   aes_byte_sink sink;
   bool finalized = false;
};

struct aes256_gcm_decoder::impl {
   ctx_ptr context = make_context();
   bytes nonce;
   bytes tag;
   bytes aad;
   aes_byte_sink sink;
   bool finalized = false;
};

aes256_key make_aes256_key(std::span<const std::uint8_t> input) {
   require_size(input.size(), aes256_key_size, "AES-256 key requires 32 bytes", aes::exceptions::code::invalid_key);

   auto key = aes256_key{};
   std::copy(input.begin(), input.end(), key.bytes.begin());
   return key;
}

aes256_key generate_aes256_key() {
   auto key = aes256_key{};
   fill_random(key.bytes);
   return key;
}

aes256_gcm_encoder::aes256_gcm_encoder(aes256_gcm_encoder_options options) : _impl(std::make_unique<impl>()) {
   require_gcm_nonce(options.nonce);
   require_sink(options.ciphertext_sink);

   _impl->nonce = std::move(options.nonce);
   _impl->aad = std::move(options.aad);
   _impl->sink = std::move(options.ciphertext_sink);

   auto out_size = int{};
   if (EVP_EncryptInit_ex(_impl->context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
       EVP_CIPHER_CTX_ctrl(_impl->context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(_impl->nonce.size()),
                           nullptr) != 1 ||
       EVP_EncryptInit_ex(_impl->context.get(), nullptr, nullptr, options.key.bytes.data(), _impl->nonce.data()) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to initialize AES-GCM encryption");
   }
   if (!_impl->aad.empty() && EVP_EncryptUpdate(_impl->context.get(), nullptr, &out_size, _impl->aad.data(),
                                                checked_update_size(_impl->aad.size(), "AES-GCM AAD")) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to apply AES-GCM AAD");
   }
}

aes256_gcm_encoder::~aes256_gcm_encoder() = default;
aes256_gcm_encoder::aes256_gcm_encoder(aes256_gcm_encoder&&) noexcept = default;
aes256_gcm_encoder& aes256_gcm_encoder::operator=(aes256_gcm_encoder&&) noexcept = default;

void aes256_gcm_encoder::write(const char* data, std::size_t size) {
   write(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(data), size});
}

void aes256_gcm_encoder::write(std::span<const std::uint8_t> data) {
   if (_impl->finalized) {
      FCL_THROW_EXCEPTION(aes::exceptions::invalid_options, "cannot write to finalized AES-GCM encoder");
   }
   if (data.empty()) {
      return;
   }

   auto out = bytes(data.size());
   auto out_size = int{};
   if (EVP_EncryptUpdate(_impl->context.get(), out.data(), &out_size, data.data(),
                         checked_update_size(data.size(), "AES-GCM plaintext")) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to encrypt AES-GCM payload");
   }
   emit_chunk(_impl->sink, out, out_size);
}

aes256_gcm_authentication aes256_gcm_encoder::finalize() {
   if (_impl->finalized) {
      FCL_THROW_EXCEPTION(aes::exceptions::invalid_options, "AES-GCM encoder already finalized");
   }
   _impl->finalized = true;

   auto out = bytes(aes_gcm_tag_size);
   auto out_size = int{};
   if (EVP_EncryptFinal_ex(_impl->context.get(), out.data(), &out_size) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to finalize AES-GCM encryption");
   }
   emit_chunk(_impl->sink, out, out_size);

   auto tag = bytes(aes_gcm_tag_size);
   if (EVP_CIPHER_CTX_ctrl(_impl->context.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to read AES-GCM tag");
   }

   return aes256_gcm_authentication{
       .nonce = _impl->nonce,
       .tag = std::move(tag),
   };
}

aes256_gcm_decoder::aes256_gcm_decoder(aes256_gcm_decoder_options options) : _impl(std::make_unique<impl>()) {
   require_gcm_nonce(options.nonce);
   require_gcm_tag(options.tag);
   require_sink(options.plaintext_sink);

   _impl->nonce = std::move(options.nonce);
   _impl->tag = std::move(options.tag);
   _impl->aad = std::move(options.aad);
   _impl->sink = std::move(options.plaintext_sink);

   auto out_size = int{};
   if (EVP_DecryptInit_ex(_impl->context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
       EVP_CIPHER_CTX_ctrl(_impl->context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(_impl->nonce.size()),
                           nullptr) != 1 ||
       EVP_DecryptInit_ex(_impl->context.get(), nullptr, nullptr, options.key.bytes.data(), _impl->nonce.data()) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to initialize AES-GCM decryption");
   }
   if (!_impl->aad.empty() && EVP_DecryptUpdate(_impl->context.get(), nullptr, &out_size, _impl->aad.data(),
                                                checked_update_size(_impl->aad.size(), "AES-GCM AAD")) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to apply AES-GCM AAD");
   }
}

aes256_gcm_decoder::~aes256_gcm_decoder() = default;
aes256_gcm_decoder::aes256_gcm_decoder(aes256_gcm_decoder&&) noexcept = default;
aes256_gcm_decoder& aes256_gcm_decoder::operator=(aes256_gcm_decoder&&) noexcept = default;

void aes256_gcm_decoder::write(const char* data, std::size_t size) {
   write(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(data), size});
}

void aes256_gcm_decoder::write(std::span<const std::uint8_t> data) {
   if (_impl->finalized) {
      FCL_THROW_EXCEPTION(aes::exceptions::invalid_options, "cannot write to finalized AES-GCM decoder");
   }
   if (data.empty()) {
      return;
   }

   auto out = bytes(data.size());
   auto out_size = int{};
   if (EVP_DecryptUpdate(_impl->context.get(), out.data(), &out_size, data.data(),
                         checked_update_size(data.size(), "AES-GCM ciphertext")) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to decrypt AES-GCM payload");
   }
   emit_chunk(_impl->sink, out, out_size);
}

void aes256_gcm_decoder::finalize() {
   if (_impl->finalized) {
      FCL_THROW_EXCEPTION(aes::exceptions::invalid_options, "AES-GCM decoder already finalized");
   }
   _impl->finalized = true;

   auto out = bytes(aes_gcm_tag_size);
   auto out_size = int{};
   if (EVP_CIPHER_CTX_ctrl(_impl->context.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(_impl->tag.size()),
                           _impl->tag.data()) != 1 ||
       EVP_DecryptFinal_ex(_impl->context.get(), out.data(), &out_size) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::authentication_failed, "AES-GCM authentication failed");
   }
   emit_chunk(_impl->sink, out, out_size);
}

aes256_gcm_ciphertext encrypt_aes256_gcm(const aes256_gcm_encrypt_request& request) {
   auto ciphertext = bytes{};
   ciphertext.reserve(request.plaintext.size());

   auto encoder = aes256_gcm_encoder{aes256_gcm_encoder_options{
       .key = request.key,
       .nonce = request.nonce,
       .aad = request.aad,
       .ciphertext_sink =
           [&](std::span<const std::uint8_t> chunk) {
              ciphertext.insert(ciphertext.end(), chunk.begin(), chunk.end());
           },
   }};
   encoder.write(request.plaintext);
   const auto authentication = encoder.finalize();

   return aes256_gcm_ciphertext{
       .nonce = authentication.nonce,
       .tag = authentication.tag,
       .ciphertext = std::move(ciphertext),
   };
}

bytes decrypt_aes256_gcm(const aes256_gcm_decrypt_request& request) {
   auto plaintext = bytes{};
   plaintext.reserve(request.encrypted.ciphertext.size());

   auto decoder = aes256_gcm_decoder{aes256_gcm_decoder_options{
       .key = request.key,
       .nonce = request.encrypted.nonce,
       .tag = request.encrypted.tag,
       .aad = request.aad,
       .plaintext_sink =
           [&](std::span<const std::uint8_t> chunk) { plaintext.insert(plaintext.end(), chunk.begin(), chunk.end()); },
   }};
   decoder.write(request.encrypted.ciphertext);
   decoder.finalize();
   return plaintext;
}

aes256_cbc_ciphertext encrypt_aes256_cbc(const aes256_cbc_encrypt_request& request) {
   require_cbc_iv(request.iv);

   auto context = make_context();
   auto out = bytes(request.plaintext.size() + aes_cbc_iv_size);
   auto out_size = int{};
   auto total_size = int{};

   if (EVP_EncryptInit_ex(context.get(), EVP_aes_256_cbc(), nullptr, request.key.bytes.data(), request.iv.data()) !=
       1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to initialize AES-CBC encryption");
   }
   if (!request.plaintext.empty() &&
       EVP_EncryptUpdate(context.get(), out.data(), &out_size, request.plaintext.data(),
                         checked_update_size(request.plaintext.size(), "AES-CBC plaintext")) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to encrypt AES-CBC payload");
   }
   total_size = out_size;
   if (EVP_EncryptFinal_ex(context.get(), out.data() + total_size, &out_size) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to finalize AES-CBC encryption");
   }
   total_size += out_size;
   out.resize(static_cast<std::size_t>(total_size));

   return aes256_cbc_ciphertext{
       .iv = request.iv,
       .ciphertext = std::move(out),
   };
}

bytes decrypt_aes256_cbc(const aes256_cbc_decrypt_request& request) {
   require_cbc_iv(request.encrypted.iv);

   auto context = make_context();
   auto out = bytes(request.encrypted.ciphertext.size());
   auto out_size = int{};
   auto total_size = int{};

   if (EVP_DecryptInit_ex(context.get(), EVP_aes_256_cbc(), nullptr, request.key.bytes.data(),
                          request.encrypted.iv.data()) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to initialize AES-CBC decryption");
   }
   if (!request.encrypted.ciphertext.empty() &&
       EVP_DecryptUpdate(context.get(), out.data(), &out_size, request.encrypted.ciphertext.data(),
                         checked_update_size(request.encrypted.ciphertext.size(), "AES-CBC ciphertext")) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::backend_error, "failed to decrypt AES-CBC payload");
   }
   total_size = out_size;
   if (EVP_DecryptFinal_ex(context.get(), out.data() + total_size, &out_size) != 1) {
      FCL_THROW_EXCEPTION(aes::exceptions::authentication_failed, "AES-CBC decryption failed");
   }
   total_size += out_size;
   out.resize(static_cast<std::size_t>(total_size));
   return out;
}

} // namespace fcl::crypto
