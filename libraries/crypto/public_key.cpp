module;
#include <fcl/exception/macros.hpp>
#include <cstdint>
#include <exception>
#include <ostream>
#include <span>
#include <string>
#include <variant>
#include <vector>

module fcl.crypto.public_key;

import fcl.core.utility;
import fcl.crypto.base58;
import fcl.crypto.common;
import fcl.crypto.exceptions;
import fcl.crypto.sha256;
import fcl.crypto.signature;
import fcl.exception.exception;
import fcl.raw.raw;
import fcl.variant.static_variant;
import fcl.variant;

namespace fcl::crypto {

struct recovery_visitor : fcl::visitor<public_key::storage_type> {
   recovery_visitor(const sha256& digest, bool check_canonical) : _digest(digest), _check_canonical(check_canonical) {}

   template <typename SignatureType> public_key::storage_type operator()(const SignatureType& s) const {
      if constexpr (requires { s.recover(_digest, _check_canonical); }) {
         return public_key::storage_type(s.recover(_digest, _check_canonical));
      } else {
         exceptions::raise(exceptions::code::invalid_options, "signature type does not support public key recovery");
      }
   }

   const sha256& _digest;
   bool _check_canonical;
};

public_key::public_key(const signature& c, const sha256& digest, bool check_canonical)
    : _storage(std::visit(recovery_visitor(digest, check_canonical), c.storage())) {}

public_key::algorithm public_key::type() const noexcept {
   switch (_storage.index()) {
   case 0:
      return algorithm::secp256k1;
   case 1:
      return algorithm::p256;
   case 2:
      return algorithm::ed25519;
   default:
      return algorithm::rsa;
   }
}

size_t public_key::which() const {
   return _storage.index();
}

static public_key::storage_type parse_base58(const std::string& base58str) {
   constexpr auto prefix = config::public_key_base_prefix;

   const auto pivot = base58str.find('_');
   FCL_ASSERT(pivot != std::string::npos, "No delimiter in string, cannot determine data type: ${str}",
              fcl::exception::ctx("str", base58str));

   const auto prefix_str = base58str.substr(0, pivot);
   FCL_ASSERT(prefix == prefix_str, "Public Key has invalid prefix", fcl::exception::ctx("str", base58str),
              fcl::exception::ctx("prefix_str", prefix_str));

   auto data_str = base58str.substr(pivot + 1);
   FCL_ASSERT(!data_str.empty(), "Public Key has no data: ${str}", fcl::exception::ctx("str", base58str));
   return base58_str_parser<public_key::storage_type, config::public_key_prefix>::apply(data_str);
}

public_key::public_key(const std::string& base58str) : _storage(parse_base58(base58str)) {}

struct is_valid_visitor : public fcl::visitor<bool> {
   template <typename KeyType> bool operator()(const KeyType& key) const {
      return key.valid();
   }
};

bool public_key::valid() const {
   return std::visit(is_valid_visitor(), _storage);
}

bool public_key::verify(std::span<const std::uint8_t> message, const signature& sig) const {
   if (_storage.index() != sig.storage().index()) {
      exceptions::raise(exceptions::code::invalid_key, "signature algorithm does not match public key");
   }

   return std::visit(
      [&](const auto& key) -> bool {
         using key_type = std::decay_t<decltype(key)>;
         const auto& typed_signature = std::get<typename key_type::signature_type>(sig.storage());
         if constexpr (std::is_same_v<key_type, secp256k1::public_key_shim>) {
            return secp256k1::verify_message(key, message, typed_signature);
         } else if constexpr (std::is_same_v<key_type, p256::public_key_shim>) {
            return p256::verify_message(key, message, typed_signature);
         } else {
            return key.verify(message, typed_signature.serialize());
         }
      },
      _storage);
}

std::string public_key::to_string(const fcl::yield_function_t& yield) const {
   auto data_str = std::visit(base58str_visitor<storage_type, config::public_key_prefix>(yield), _storage);
   return std::string(config::public_key_base_prefix) + "_" + data_str;
}

std::ostream& operator<<(std::ostream& s, const public_key& k) {
   s << "public_key(" << k.to_string({}) << ')';
   return s;
}

bool operator==(const public_key& p1, const public_key& p2) {
   return eq_comparator<public_key::storage_type>::apply(p1._storage, p2._storage);
}

bool operator!=(const public_key& p1, const public_key& p2) {
   return !(p1 == p2);
}

bool operator<(const public_key& p1, const public_key& p2) {
   return less_comparator<public_key::storage_type>::apply(p1._storage, p2._storage);
}
} // namespace fcl::crypto

namespace fcl::crypto {
using namespace std;
void to_variant(const fcl::crypto::public_key& var, fcl::variant& vo, const fcl::yield_function_t& yield) {
   vo = var.to_string(yield);
}

void from_variant(const fcl::variant& var, fcl::crypto::public_key& vo) {
   vo = fcl::crypto::public_key(var.as_string());
}
} // namespace fcl::crypto
