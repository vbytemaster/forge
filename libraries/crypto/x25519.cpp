module;

#include <fcl/exceptions/macros.hpp>

#include <openssl/evp.h>

#include <memory>

module fcl.crypto.x25519;

namespace fcl::crypto::x25519 {
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

using pkey_ptr = std::unique_ptr<EVP_PKEY, pkey_deleter>;
using pkey_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, pkey_ctx_deleter>;

[[noreturn]] void fail(std::string message) {
   FCL_THROW_EXCEPTION(exceptions::backend_error, std::move(message));
}

[[nodiscard]] pkey_ptr make_private(const private_key_secret& secret) {
   auto* raw = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, secret.data(), secret.size());
   if (raw == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "invalid X25519 private key");
   }
   return pkey_ptr{raw};
}

[[nodiscard]] pkey_ptr make_public(const public_key_data& data) {
   auto* raw = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, data.data(), data.size());
   if (raw == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "invalid X25519 public key");
   }
   return pkey_ptr{raw};
}

} // namespace

public_key::public_key(const public_key_data& value) : data_(value) {}

const public_key_data& public_key::serialize() const noexcept {
   return data_;
}

private_key::private_key(const private_key_secret& value) : data_(value) {}

private_key private_key::generate() {
   auto context = pkey_ctx_ptr{EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr)};
   if (context == nullptr || EVP_PKEY_keygen_init(context.get()) != 1) {
      fail("failed to initialize X25519 key generation");
   }
   EVP_PKEY* raw = nullptr;
   if (EVP_PKEY_keygen(context.get(), &raw) != 1 || raw == nullptr) {
      fail("failed to generate X25519 key");
   }
   auto key = pkey_ptr{raw};
   auto out = private_key_secret{};
   auto size = out.size();
   if (EVP_PKEY_get_raw_private_key(key.get(), out.data(), &size) != 1 || size != out.size()) {
      fail("failed to export X25519 private key");
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
      fail("failed to export X25519 public key");
   }
   return public_key{out};
}

shared_secret private_key::get_shared_secret(const public_key& remote) const {
   auto local = make_private(data_);
   auto peer = make_public(remote.serialize());
   auto context = pkey_ctx_ptr{EVP_PKEY_CTX_new(local.get(), nullptr)};
   if (context == nullptr || EVP_PKEY_derive_init(context.get()) != 1 ||
       EVP_PKEY_derive_set_peer(context.get(), peer.get()) != 1) {
      fail("failed to initialize X25519 secret agreement");
   }
   auto out = shared_secret{};
   auto size = out.size();
   if (EVP_PKEY_derive(context.get(), out.data(), &size) != 1 || size != out.size()) {
      fail("failed to derive X25519 shared secret");
   }
   return out;
}

} // namespace fcl::crypto::x25519
