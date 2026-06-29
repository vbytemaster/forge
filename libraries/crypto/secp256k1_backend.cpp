module;
#include <forge/exceptions/macros.hpp>
#include <exception>
#include <memory>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <span>
#include <utility>

module forge.crypto.secp256k1;

import forge.crypto.sha256;
import forge.exceptions;
#include "details/private_key_impl.hxx"

/* used by mixed + secp256k1 */

namespace forge::crypto::secp256k1 {
namespace detail {

private_key_impl::private_key_impl() noexcept {}

private_key_impl::private_key_impl(const private_key_impl& cpy) noexcept {
   this->_key = cpy._key;
}

private_key_impl& private_key_impl::operator=(const private_key_impl& pk) noexcept {
   _key = pk._key;
   return *this;
}
} // namespace detail

static const private_key_secret empty_priv{};

private_key::private_key() : my(std::make_unique<detail::private_key_impl>()) {}

private_key::private_key(const private_key& pk)
    : my(pk.my ? std::make_unique<detail::private_key_impl>(*pk.my) : nullptr) {}

private_key::private_key(private_key&& pk) : my(std::move(pk.my)) {}

private_key::~private_key() {}

private_key& private_key::operator=(private_key&& pk) {
   my = std::move(pk.my);
   return *this;
}

private_key& private_key::operator=(const private_key& pk) {
   my = pk.my ? std::make_unique<detail::private_key_impl>(*pk.my) : nullptr;
   return *this;
}

private_key private_key::regenerate(const forge::crypto::sha256& secret) {
   private_key self;
   self.my->_key = secret;
   return self;
}

forge::crypto::sha256 private_key::get_secret() const {
   return my->_key;
}

public_key private_key::get_public_key() const {
   FORGE_ASSERT(my->_key != empty_priv);
   public_key_data pub;
   size_t pub_len = sizeof(pub);
   secp256k1_pubkey secp_pub;
   FORGE_ASSERT(secp256k1_ec_pubkey_create(detail::_get_context(), &secp_pub, (unsigned char*)my->_key.data()));
   secp256k1_ec_pubkey_serialize(detail::_get_context(), (unsigned char*)&pub, &pub_len, &secp_pub,
                                 SECP256K1_EC_COMPRESSED);
   FORGE_ASSERT(pub_len == pub.size());
   return public_key(pub);
}

static int extended_nonce_function(unsigned char* nonce32, const unsigned char* msg32, const unsigned char* key32,
                                   const unsigned char* algo16, void* data, unsigned int attempt) {
   unsigned int* extra = (unsigned int*)data;
   (*extra)++;
   return secp256k1_nonce_function_default(nonce32, msg32, key32, algo16, nullptr, *extra);
}

compact_signature private_key::sign_compact(const forge::crypto::sha256& digest, bool require_canonical) const {
   FORGE_ASSERT(my->_key != empty_priv);
   compact_signature result;
   secp256k1_ecdsa_recoverable_signature secp_sig;
   int recid;
   unsigned int counter = 0;
   do {
      FORGE_ASSERT(secp256k1_ecdsa_sign_recoverable(detail::_get_context(), &secp_sig, (unsigned char*)digest.data(),
                                                  (unsigned char*)my->_key.data(), extended_nonce_function, &counter));
      secp256k1_ecdsa_recoverable_signature_serialize_compact(detail::_get_context(), result.data() + 1, &recid,
                                                              &secp_sig);
   } while (require_canonical && !public_key::is_canonical(result));

   result.begin()[0] = 27 + 4 + recid;
   return result;
}

der_signature sign_der(const private_key_shim& key, std::span<const std::uint8_t> message) {
   const auto digest = forge::crypto::sha256::hash(message);
   const auto secret = key.serialize();
   if (!secp256k1_ec_seckey_verify(detail::_get_context(), reinterpret_cast<const unsigned char*>(secret.data()))) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_input, "invalid secp256k1 private key");
   }

   secp256k1_ecdsa_signature signature;
   if (secp256k1_ecdsa_sign(detail::_get_context(), &signature, reinterpret_cast<const unsigned char*>(digest.data()),
                            reinterpret_cast<const unsigned char*>(secret.data()), nullptr, nullptr) != 1) {
      FORGE_THROW_EXCEPTION(exceptions::backend_error, "failed to create secp256k1 DER signature");
   }
   secp256k1_ecdsa_signature normalized;
   secp256k1_ecdsa_signature_normalize(detail::_get_context(), &normalized, &signature);

   auto out = der_signature(72);
   auto size = out.size();
   if (secp256k1_ecdsa_signature_serialize_der(detail::_get_context(), out.data(), &size, &normalized) != 1) {
      FORGE_THROW_EXCEPTION(exceptions::backend_error, "failed to serialize secp256k1 DER signature");
   }
   out.resize(size);
   return out;
}

bool verify_der(const public_key_shim& key, std::span<const std::uint8_t> message,
                std::span<const std::uint8_t> signature_bytes) {
   secp256k1_pubkey public_key;
   const auto public_key_data = key.serialize();
   if (secp256k1_ec_pubkey_parse(detail::_get_context(), &public_key,
                                 reinterpret_cast<const unsigned char*>(public_key_data.data()),
                                 public_key_data.size()) != 1) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_input, "invalid secp256k1 public key");
   }

   secp256k1_ecdsa_signature signature;
   if (secp256k1_ecdsa_signature_parse_der(detail::_get_context(), &signature, signature_bytes.data(),
                                           signature_bytes.size()) != 1) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_signature, "invalid secp256k1 DER signature");
   }
   secp256k1_ecdsa_signature normalized;
   secp256k1_ecdsa_signature_normalize(detail::_get_context(), &normalized, &signature);
   const auto digest = forge::crypto::sha256::hash(message);
   return secp256k1_ecdsa_verify(detail::_get_context(), &normalized,
                                 reinterpret_cast<const unsigned char*>(digest.data()), &public_key) == 1;
}

} // namespace forge::crypto::secp256k1
