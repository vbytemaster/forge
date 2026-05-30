module;

#include <fcl/exception/macros.hpp>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <memory>
#include <span>

module fcl.crypto.rsa;

namespace fcl::crypto::rsa {
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
   FCL_THROW_EXCEPTION(exceptions::backend_error, std::move(message));
}

[[nodiscard]] pkey_ptr read_public(std::span<const std::uint8_t> der) {
   const auto* cursor = der.data();
   auto* raw = d2i_PUBKEY(nullptr, &cursor, static_cast<long>(der.size()));
   if (raw == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "invalid RSA public key DER");
   }
   return pkey_ptr{raw};
}

[[nodiscard]] pkey_ptr read_private(std::span<const std::uint8_t> der) {
   const auto* cursor = der.data();
   auto* raw = d2i_AutoPrivateKey(nullptr, &cursor, static_cast<long>(der.size()));
   if (raw == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "invalid RSA private key DER");
   }
   return pkey_ptr{raw};
}

[[nodiscard]] bytes write_public(EVP_PKEY* key) {
   const auto size = i2d_PUBKEY(key, nullptr);
   if (size <= 0) {
      fail("failed to size RSA public key DER");
   }
   auto out = bytes(static_cast<std::size_t>(size));
   auto* cursor = out.data();
   if (i2d_PUBKEY(key, &cursor) != size) {
      fail("failed to export RSA public key DER");
   }
   return out;
}

[[nodiscard]] bytes write_private(EVP_PKEY* key) {
   const auto size = i2d_PrivateKey(key, nullptr);
   if (size <= 0) {
      fail("failed to size RSA private key DER");
   }
   auto out = bytes(static_cast<std::size_t>(size));
   auto* cursor = out.data();
   if (i2d_PrivateKey(key, &cursor) != size) {
      fail("failed to export RSA private key DER");
   }
   return out;
}

} // namespace

public_key::public_key(public_key_data value) : data_(std::move(value)) {}

const public_key_data& public_key::serialize() const noexcept {
   return data_;
}

bool public_key::valid() const noexcept {
   return !data_.empty();
}

bool public_key::verify(std::span<const std::uint8_t> message, const signature_data& signature) const {
   auto key = read_public(data_);
   auto context = md_ctx_ptr{EVP_MD_CTX_new()};
   if (context == nullptr || EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1 ||
       EVP_DigestVerifyUpdate(context.get(), message.data(), message.size()) != 1) {
      fail("failed to initialize RSA verifier");
   }
   return EVP_DigestVerifyFinal(context.get(), signature.data(), signature.size()) == 1;
}

private_key::private_key(private_key_secret value) : data_(std::move(value)) {}

private_key private_key::generate(std::uint32_t bits) {
   auto context = pkey_ctx_ptr{EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr)};
   if (context == nullptr || EVP_PKEY_keygen_init(context.get()) != 1 ||
       EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), static_cast<int>(bits)) != 1) {
      fail("failed to initialize RSA key generation");
   }
   EVP_PKEY* raw = nullptr;
   if (EVP_PKEY_keygen(context.get(), &raw) != 1 || raw == nullptr) {
      fail("failed to generate RSA key");
   }
   auto key = pkey_ptr{raw};
   return private_key{write_private(key.get())};
}

private_key private_key::regenerate(private_key_secret value) {
   return private_key{std::move(value)};
}

const private_key_secret& private_key::get_secret() const noexcept {
   return data_;
}

public_key private_key::get_public_key() const {
   auto key = read_private(data_);
   return public_key{write_public(key.get())};
}

signature_data private_key::sign(std::span<const std::uint8_t> message) const {
   auto key = read_private(data_);
   auto context = md_ctx_ptr{EVP_MD_CTX_new()};
   if (context == nullptr || EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1 ||
       EVP_DigestSignUpdate(context.get(), message.data(), message.size()) != 1) {
      fail("failed to initialize RSA signer");
   }
   auto size = std::size_t{};
   if (EVP_DigestSignFinal(context.get(), nullptr, &size) != 1) {
      fail("failed to size RSA signature");
   }
   auto out = signature_data(size);
   if (EVP_DigestSignFinal(context.get(), out.data(), &size) != 1) {
      fail("failed to sign RSA message");
   }
   out.resize(size);
   return out;
}

bool public_key_shim::valid() const noexcept {
   return !_data.empty();
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

} // namespace fcl::crypto::rsa
