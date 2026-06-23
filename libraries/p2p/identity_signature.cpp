module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

module forge.p2p.identity;

import forge.crypto.asymmetric;
import forge.crypto.der;
import forge.crypto.ed25519;
import forge.crypto.p256;
import forge.crypto.rsa;
import forge.crypto.secp256k1;
import forge.exceptions;
import forge.p2p.exceptions;

#include "identity_signature.hpp"

namespace forge::p2p {
namespace {

template <typename Range> [[nodiscard]] std::vector<std::uint8_t> bytes_from_range(const Range& value) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(value.size());
   for (const auto byte : value) {
      out.push_back(static_cast<std::uint8_t>(byte));
   }
   return out;
}

[[noreturn]] void throw_identity(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_identity, std::move(message));
}

[[nodiscard]] forge::crypto::ed25519::public_key_data ed25519_public_key_data(const public_key& key) {
   if (key.data.size() != forge::crypto::ed25519::public_key_data{}.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "invalid Ed25519 public key size");
   }
   auto out = forge::crypto::ed25519::public_key_data{};
   std::copy(key.data.begin(), key.data.end(), out.begin());
   return out;
}

[[nodiscard]] forge::crypto::secp256k1::public_key_data secp256k1_public_key_data(const public_key& key) {
   if (key.data.size() != forge::crypto::secp256k1::public_key_data{}.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "invalid secp256k1 public key size");
   }
   auto out = forge::crypto::secp256k1::public_key_data{};
   std::copy(key.data.begin(), key.data.end(), out.begin());
   return out;
}

} // namespace

} // namespace forge::p2p

extern "C++" {
namespace forge::p2p {

public_key public_key_from_crypto(const forge::crypto::asymmetric::public_key& key) {
   return key.visit([](const auto& value) -> public_key {
      using value_type = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<value_type, forge::crypto::ed25519::public_key_shim>) {
         return public_key{.type = public_key::type::ed25519, .data = bytes_from_range(value.serialize())};
      } else if constexpr (std::is_same_v<value_type, forge::crypto::rsa::public_key_shim>) {
         return public_key{.type = public_key::type::rsa, .data = value.serialize()};
      } else if constexpr (std::is_same_v<value_type, forge::crypto::secp256k1::public_key_shim>) {
         return public_key{.type = public_key::type::secp256k1, .data = bytes_from_range(value.serialize())};
      } else if constexpr (std::is_same_v<value_type, forge::crypto::p256::public_key_shim>) {
         const auto spki = forge::crypto::der::write_public_key(forge::crypto::asymmetric::public_key{
             forge::crypto::asymmetric::public_key::storage_type{value}});
         return public_key{.type = public_key::type::ecdsa, .data = spki};
      }
   });
}

forge::crypto::asymmetric::public_key crypto_public_key(const public_key& key) {
   if (key.data.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p public key is empty");
   }

   switch (key.type) {
   case public_key::type::ed25519:
      return forge::crypto::asymmetric::public_key{
          forge::crypto::asymmetric::public_key::storage_type{
              forge::crypto::ed25519::public_key_shim{ed25519_public_key_data(key)}}};
   case public_key::type::rsa:
      return forge::crypto::asymmetric::public_key{
          forge::crypto::asymmetric::public_key::storage_type{forge::crypto::rsa::public_key_shim{key.data}}};
   case public_key::type::secp256k1:
      return forge::crypto::asymmetric::public_key{
          forge::crypto::asymmetric::public_key::storage_type{
              forge::crypto::secp256k1::public_key_shim{secp256k1_public_key_data(key)}}};
   case public_key::type::ecdsa: {
      try {
         auto parsed = forge::crypto::der::read_public_key(key.data);
         if (parsed.type() != forge::crypto::asymmetric::algorithm::p256) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p ECDSA public key must be P-256");
         }
         return parsed;
      } catch (const forge::exceptions::base& error) {
         throw_identity(error.what());
      }
   }
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "unsupported libp2p public key type");
}

std::vector<std::uint8_t> sign_identity(const forge::crypto::asymmetric::private_key& key,
                                        std::span<const std::uint8_t> message) {
   try {
      return key.visit([&](const auto& value) -> std::vector<std::uint8_t> {
         using value_type = std::decay_t<decltype(value)>;
         if constexpr (std::is_same_v<value_type, forge::crypto::secp256k1::private_key_shim>) {
            return forge::crypto::secp256k1::sign_der(value, message);
         } else if constexpr (std::is_same_v<value_type, forge::crypto::p256::private_key_shim>) {
            return forge::crypto::p256::sign_der(value, message);
         } else {
            return bytes_from_range(value.sign(message).serialize());
         }
      });
   } catch (const forge::exceptions::base& error) {
      throw_identity(error.what());
   }
}

bool verify_identity_signature(const public_key& key, std::span<const std::uint8_t> message,
                               std::span<const std::uint8_t> signature) {
   try {
      switch (key.type) {
      case public_key::type::ed25519: {
         if (signature.size() != forge::crypto::ed25519::signature_data{}.size()) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "invalid Ed25519 signature size");
         }
         auto value = forge::crypto::ed25519::signature_data{};
         std::copy(signature.begin(), signature.end(), value.begin());
         return forge::crypto::ed25519::public_key{ed25519_public_key_data(key)}.verify(message, value);
      }
      case public_key::type::rsa:
         return forge::crypto::rsa::public_key{key.data}.verify(message, {signature.begin(), signature.end()});
      case public_key::type::secp256k1:
         return forge::crypto::secp256k1::verify_der(
             forge::crypto::secp256k1::public_key_shim{secp256k1_public_key_data(key)}, message, signature);
      case public_key::type::ecdsa:
         return forge::crypto::p256::verify_der(
             crypto_public_key(key).as<forge::crypto::p256::public_key_shim>(), message, signature);
      }
   } catch (const forge::exceptions::base& error) {
      throw_identity(error.what());
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "unsupported libp2p public key type");
}

} // namespace forge::p2p
}
