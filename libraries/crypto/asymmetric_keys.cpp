module;
#include <algorithm>
#include <compare>
#include <cstddef>
#include <ostream>
#include <span>
#include <type_traits>
#include <variant>

module fcl.crypto.asymmetric;

import fcl.core.utility;
import fcl.crypto.exceptions;
import fcl.exception.exception;
import fcl.variant.static_variant;

namespace fcl::crypto::asymmetric {
namespace {

template <typename Storage> [[nodiscard]] algorithm algorithm_for(const Storage& storage) noexcept {
   switch (storage.index()) {
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

template <typename Storage> [[nodiscard]] bool storage_equal(const Storage& left, const Storage& right) {
   if (left.index() != right.index()) {
      return false;
   }
   return std::visit(
      [&](const auto& value) {
         using value_type = std::decay_t<decltype(value)>;
         const auto& left_data = value.serialize();
         const auto& right_data = std::get<value_type>(right).serialize();
         if constexpr (requires { left_data.size(); left_data.begin(); }) {
            return left_data.size() == right_data.size() && std::equal(left_data.begin(), left_data.end(), right_data.begin());
         } else {
            return left_data == right_data;
         }
      },
      left);
}

template <typename Storage> [[nodiscard]] bool storage_less(const Storage& left, const Storage& right) {
   if (left.index() != right.index()) {
      return left.index() < right.index();
   }
   return std::visit(
      [&](const auto& value) {
         using value_type = std::decay_t<decltype(value)>;
         const auto& left_data = value.serialize();
         const auto& right_data = std::get<value_type>(right).serialize();
         if constexpr (requires { left_data.begin(); left_data.end(); }) {
            return std::lexicographical_compare(left_data.begin(), left_data.end(), right_data.begin(), right_data.end());
         } else {
            return (left_data <=> right_data) < 0;
         }
      },
      left);
}

struct public_key_visitor : visitor<public_key::storage_type> {
   template <typename KeyType> public_key::storage_type operator()(const KeyType& key) const {
      return public_key::storage_type(key.get_public_key());
   }
};

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

struct sign_visitor : visitor<signature::storage_type> {
   explicit sign_visitor(std::span<const std::uint8_t> message) : _message(message) {}

   template <typename KeyType> signature::storage_type operator()(const KeyType& key) const {
      return signature::storage_type(key.sign(_message));
   }

   std::span<const std::uint8_t> _message;
};

struct is_valid_visitor : public fcl::visitor<bool> {
   template <typename KeyType> bool operator()(const KeyType& key) const {
      return key.valid();
   }
};

struct hash_visitor : public fcl::visitor<std::size_t> {
   template <typename SigType> std::size_t operator()(const SigType& sig) const {
      auto seed = std::size_t{0};
      const auto& data = sig.serialize();
      for (auto byte : data) {
         seed ^= static_cast<std::size_t>(byte) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
      }
      return seed;
   }
};

} // namespace

public_key private_key::get_public_key() const {
   return public_key(std::visit(public_key_visitor(), _storage));
}

algorithm private_key::type() const noexcept {
   return algorithm_for(_storage);
}

signature private_key::sign(std::span<const std::uint8_t> message) const {
   return signature(std::visit(sign_visitor(message), _storage));
}

bool operator==(const private_key& p1, const private_key& p2) {
   return storage_equal(p1._storage, p2._storage);
}

bool operator<(const private_key& p1, const private_key& p2) {
   return storage_less(p1._storage, p2._storage);
}

public_key::public_key(const signature& c, const sha256& digest, bool check_canonical)
    : _storage(std::visit(recovery_visitor(digest, check_canonical), c.storage())) {}

algorithm public_key::type() const noexcept {
   return algorithm_for(_storage);
}

std::size_t public_key::which() const {
   return _storage.index();
}

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

std::ostream& operator<<(std::ostream& s, const public_key& k) {
   s << "public_key(" << k.to_string({}) << ')';
   return s;
}

bool operator==(const public_key& p1, const public_key& p2) {
   return storage_equal(p1._storage, p2._storage);
}

bool operator!=(const public_key& p1, const public_key& p2) {
   return !(p1 == p2);
}

bool operator<(const public_key& p1, const public_key& p2) {
   return storage_less(p1._storage, p2._storage);
}

algorithm signature::type() const noexcept {
   return algorithm_for(_storage);
}

std::size_t signature::which() const {
   return _storage.index();
}

std::size_t signature::variable_size() const {
   return std::visit([](const auto& sig) { return sig.serialize().size(); }, _storage);
}

bool operator==(const signature& p1, const signature& p2) {
   return storage_equal(p1._storage, p2._storage);
}

bool operator!=(const signature& p1, const signature& p2) {
   return !storage_equal(p1._storage, p2._storage);
}

bool operator<(const signature& p1, const signature& p2) {
   return storage_less(p1._storage, p2._storage);
}

std::size_t hash_value(const signature& b) {
   return std::visit(hash_visitor(), b._storage);
}

} // namespace fcl::crypto::asymmetric
