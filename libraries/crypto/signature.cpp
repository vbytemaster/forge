module;
#include <fcl/exception/macros.hpp>
#include <exception>
#include <ostream>
#include <string>
#include <type_traits>
#include <variant>

module fcl.crypto.signature;

import fcl.core.utility;
import fcl.crypto.common;
import fcl.exception.exception;
import fcl.variant.static_variant;
import fcl.variant;

namespace fcl::crypto {
struct hash_visitor : public fcl::visitor<size_t> {
   template <typename SigType> size_t operator()(const SigType& sig) const {
      auto seed = std::size_t{0};
      const auto& data = sig.serialize();
      for (auto byte : data) {
         seed ^= static_cast<std::size_t>(byte) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
      }
      return seed;
   }
};

signature::algorithm signature::type() const noexcept {
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

static signature::storage_type sig_parse_base58(const std::string& base58str) {
   try {
      constexpr auto prefix = config::signature_base_prefix;

      const auto pivot = base58str.find('_');
      FCL_ASSERT(pivot != std::string::npos, "No delimiter in string, cannot determine type: ${str}",
                 fcl::exception::ctx("str", base58str));

      const auto prefix_str = base58str.substr(0, pivot);
      FCL_ASSERT(prefix == prefix_str, "Signature Key has invalid prefix", fcl::exception::ctx("str", base58str),
                 fcl::exception::ctx("prefix_str", prefix_str));

      auto data_str = base58str.substr(pivot + 1);
      FCL_ASSERT(!data_str.empty(), "Signature has no data: ${str}", fcl::exception::ctx("str", base58str));
      return base58_str_parser<signature::storage_type, config::signature_prefix>::apply(data_str);
   }
   FCL_CAPTURE_AND_RETHROW("error parsing signature", fcl::exception::ctx("str", base58str))
}

signature::signature(const std::string& base58str) : _storage(sig_parse_base58(base58str)) {}

size_t signature::which() const {
   return _storage.index();
}

template <class... Ts> struct overloaded : Ts... {
   using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

size_t signature::variable_size() const {
   return std::visit([](const auto& sig) { return sig.serialize().size(); }, _storage);
}

std::string signature::to_string(const fcl::yield_function_t& yield) const {
   auto data_str = std::visit(base58str_visitor<storage_type, config::signature_prefix>(yield), _storage);
   yield();
   return std::string(config::signature_base_prefix) + "_" + data_str;
}

std::ostream& operator<<(std::ostream& s, const signature& k) {
   s << "signature(" << k.to_string() << ')';
   return s;
}

bool operator==(const signature& p1, const signature& p2) {
   return eq_comparator<signature::storage_type>::apply(p1._storage, p2._storage);
}

bool operator!=(const signature& p1, const signature& p2) {
   return !eq_comparator<signature::storage_type>::apply(p1._storage, p2._storage);
}

bool operator<(const signature& p1, const signature& p2) {
   return less_comparator<signature::storage_type>::apply(p1._storage, p2._storage);
}

size_t hash_value(const signature& b) {
   return std::visit(hash_visitor(), b._storage);
}
} // namespace fcl::crypto

namespace fcl::crypto {
void to_variant(const fcl::crypto::signature& var, fcl::variant& vo, const fcl::yield_function_t& yield) {
   vo = var.to_string(yield);
}

void from_variant(const fcl::variant& var, fcl::crypto::signature& vo) {
   vo = fcl::crypto::signature(var.as_string());
}
} // namespace fcl::crypto
