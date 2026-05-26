module;
#include <fcl/exception/macros.hpp>
#include <cstring>
#include <exception>
#include <span>
#include <string>
#include <variant>
#include <vector>

module fcl.crypto.private_key;

import fcl.core.utility;
import fcl.crypto.base58;
import fcl.crypto.common;
import fcl.crypto.public_key;
import fcl.crypto.sha256;
import fcl.crypto.sha512;
import fcl.crypto.signature;
import fcl.exception.exception;
import fcl.variant.static_variant;
import fcl.variant;

namespace fcl::crypto {
using namespace std;

struct public_key_visitor : visitor<public_key::storage_type> {
   template <typename KeyType> public_key::storage_type operator()(const KeyType& key) const {
      return public_key::storage_type(key.get_public_key());
   }
};

public_key private_key::get_public_key() const {
   return public_key(std::visit(public_key_visitor(), _storage));
}

private_key::algorithm private_key::type() const noexcept {
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

struct sign_visitor : visitor<signature::storage_type> {
   explicit sign_visitor(std::span<const std::uint8_t> message) : _message(message) {}

   template <typename KeyType> signature::storage_type operator()(const KeyType& key) const {
      return signature::storage_type(key.sign(_message));
   }

   std::span<const std::uint8_t> _message;
};

signature private_key::sign(std::span<const std::uint8_t> message) const {
   return signature(std::visit(sign_visitor(message), _storage));
}

template <typename Data> string to_wif(const Data& secret, const fcl::yield_function_t& yield) {
   const size_t size_of_data_to_hash = sizeof(typename Data::data_type) + 1;
   const size_t size_of_hash_bytes = 4;
   char data[size_of_data_to_hash + size_of_hash_bytes];
   data[0] = (char)0x80; // this is the Bitcoin MainNet code
   memcpy(&data[1], (const char*)&secret.serialize(), sizeof(typename Data::data_type));
   sha256 digest = sha256::hash(data, size_of_data_to_hash);
   digest = sha256::hash(digest);
   memcpy(data + size_of_data_to_hash, (char*)&digest, size_of_hash_bytes);
   return to_base58(data, sizeof(data), yield);
}

template <typename Data> Data from_wif(const string& wif_key) {
   auto wif_bytes = from_base58(wif_key);
   FCL_ASSERT(wif_bytes.size() >= 5);
   auto key_bytes = vector<char>(wif_bytes.begin() + 1, wif_bytes.end() - 4);
   fcl::crypto::sha256 check = fcl::crypto::sha256::hash(wif_bytes.data(), wif_bytes.size() - 4);
   fcl::crypto::sha256 check2 = fcl::crypto::sha256::hash(check);

   FCL_ASSERT(memcmp((char*)&check, wif_bytes.data() + wif_bytes.size() - 4, 4) == 0 ||
              memcmp((char*)&check2, wif_bytes.data() + wif_bytes.size() - 4, 4) == 0);

   return Data(fcl::variant(key_bytes).as<typename Data::data_type>());
}

static private_key::storage_type priv_parse_base58(const string& base58str) {
   const auto pivot = base58str.find('_');

   constexpr auto prefix = config::private_key_base_prefix;
   FCL_ASSERT(pivot != std::string::npos, "No delimiter in string, cannot determine private key type",
              fcl::exception::ctx("str", base58str));
   const auto prefix_str = base58str.substr(0, pivot);
   FCL_ASSERT(prefix == prefix_str, "Private Key has invalid prefix", fcl::exception::ctx("str", base58str),
              fcl::exception::ctx("prefix_str", prefix_str));

   auto data_str = base58str.substr(pivot + 1);
   FCL_ASSERT(!data_str.empty(), "Private Key has no data: ${str}", fcl::exception::ctx("str", base58str));
   return base58_str_parser<private_key::storage_type, config::private_key_prefix>::apply(data_str);
}

private_key::private_key(const std::string& base58str) : _storage(priv_parse_base58(base58str)) {}

std::string private_key::to_string(const fcl::yield_function_t& yield) const {
   auto data_str = std::visit(base58str_visitor<storage_type, config::private_key_prefix>(yield), _storage);
   return std::string(config::private_key_base_prefix) + "_" + data_str;
}

bool operator==(const private_key& p1, const private_key& p2) {
   return eq_comparator<private_key::storage_type>::apply(p1._storage, p2._storage);
}

bool operator<(const private_key& p1, const private_key& p2) {
   return less_comparator<private_key::storage_type>::apply(p1._storage, p2._storage);
}
} // namespace fcl::crypto

namespace fcl::crypto {
void to_variant(const fcl::crypto::private_key& var, variant& vo, const fcl::yield_function_t& yield) {
   vo = var.to_string(yield);
}

void from_variant(const variant& var, fcl::crypto::private_key& vo) {
   vo = fcl::crypto::private_key(var.as_string());
}

} // namespace fcl::crypto
