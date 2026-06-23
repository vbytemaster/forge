module;

#include <forge/exceptions/macros.hpp>

#include <openssl/evp.h>

#include <algorithm>
#include <memory>
#include <span>

module forge.crypto.ed25519;

namespace forge::crypto::ed25519 {
namespace {

struct pkey_deleter {
   void operator()(EVP_PKEY* value) const noexcept {
      EVP_PKEY_free(value);
   }
};

struct pkey_ctx_deleter {
   void operator()(EVP_PKEY_CTX* value) const noexcept {
      EVP_PKEY_CTX_free(value);
   }
};

struct md_ctx_deleter {
   void operator()(EVP_MD_CTX* value) const noexcept {
      EVP_MD_CTX_free(value);
   }
};

using pkey_ptr = std::unique_ptr<EVP_PKEY, pkey_deleter>;
using pkey_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, pkey_ctx_deleter>;
using md_ctx_ptr = std::unique_ptr<EVP_MD_CTX, md_ctx_deleter>;

[[noreturn]] void fail(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::backend_error, std::move(message));
}

[[nodiscard]] pkey_ptr make_private(const private_key_secret& secret) {
   auto* raw = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, secret.data(), secret.size());
   if (raw == nullptr) {
      fail("failed to parse Ed25519 private key");
   }
   return pkey_ptr{raw};
}

[[nodiscard]] pkey_ptr make_public(const public_key_data& data) {
   auto* raw = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, data.data(), data.size());
   if (raw == nullptr) {
      fail("failed to parse Ed25519 public key");
   }
   return pkey_ptr{raw};
}

} // namespace

public_key::public_key(const public_key_data& value) : data_(value) {}

const public_key_data& public_key::serialize() const noexcept {
   return data_;
}

bool public_key::valid() const noexcept {
   return std::any_of(data_.begin(), data_.end(), [](auto value) { return value != 0; });
}

bool public_key::verify(std::span<const std::uint8_t> message, const signature_data& signature) const {
   auto key = make_public(data_);
   auto context = md_ctx_ptr{EVP_MD_CTX_new()};
   if (context == nullptr || EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
      fail("failed to initialize Ed25519 verifier");
   }
   return EVP_DigestVerify(context.get(), signature.data(), signature.size(), message.data(), message.size()) == 1;
}

private_key::private_key(const private_key_secret& value) : data_(value) {}

private_key private_key::generate() {
   auto context = pkey_ctx_ptr{EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr)};
   if (context == nullptr || EVP_PKEY_keygen_init(context.get()) != 1) {
      fail("failed to initialize Ed25519 key generation");
   }
   EVP_PKEY* raw = nullptr;
   if (EVP_PKEY_keygen(context.get(), &raw) != 1 || raw == nullptr) {
      fail("failed to generate Ed25519 key");
   }
   auto key = pkey_ptr{raw};
   auto out = private_key_secret{};
   auto size = out.size();
   if (EVP_PKEY_get_raw_private_key(key.get(), out.data(), &size) != 1 || size != out.size()) {
      fail("failed to export Ed25519 private key");
   }
   return private_key{out};
}

private_key private_key::regenerate(const private_key_secret& value) {
   return private_key{value};
}

const private_key_secret& private_key::get_secret() const noexcept {
   return data_;
}

public_key private_key::get_public_key() const {
   auto key = make_private(data_);
   auto out = public_key_data{};
   auto size = out.size();
   if (EVP_PKEY_get_raw_public_key(key.get(), out.data(), &size) != 1 || size != out.size()) {
      fail("failed to export Ed25519 public key");
   }
   return public_key{out};
}

signature_data private_key::sign(std::span<const std::uint8_t> message) const {
   auto key = make_private(data_);
   auto context = md_ctx_ptr{EVP_MD_CTX_new()};
   if (context == nullptr || EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
      fail("failed to initialize Ed25519 signer");
   }
   auto out = signature_data{};
   auto size = out.size();
   if (EVP_DigestSign(context.get(), out.data(), &size, message.data(), message.size()) != 1 || size != out.size()) {
      fail("failed to sign Ed25519 message");
   }
   return out;
}

bool public_key_shim::valid() const noexcept {
   return public_key{_data}.valid();
}

bool public_key_shim::verify(std::span<const std::uint8_t> message, const signature_data& signature) const {
   return public_key{_data}.verify(message, signature);
}

private_key_shim::signature_type private_key_shim::sign(std::span<const std::uint8_t> message) const {
   return signature_type{private_key::regenerate(_data).sign(message)};
}

private_key_shim::public_key_type private_key_shim::get_public_key() const {
   return public_key_type{private_key::regenerate(_data).get_public_key().serialize()};
}

private_key_shim private_key_shim::generate() {
   return private_key_shim{private_key::generate().get_secret()};
}

} // namespace forge::crypto::ed25519
