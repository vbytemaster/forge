module;
#include <fcl/exceptions/macros.hpp>
#include <algorithm>
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>

module fcl.crypto.bls;

import fcl.exceptions;

namespace fcl::crypto::bls {

inline std::array<uint8_t, 96> deserialize_base64url(const std::string& base64urlstr) {
   auto res =
       std::mismatch(config::public_key_prefix.begin(), config::public_key_prefix.end(), base64urlstr.begin());
   FCL_ASSERT(res.first == config::public_key_prefix.end(), "BLS Public Key has invalid format : ${str}",
              fcl::exceptions::ctx("str", base64urlstr));
   auto data_str = base64urlstr.substr(config::public_key_prefix.size());
   return fcl::crypto::bls::detail::deserialize_base64url<std::array<uint8_t, 96>>(data_str);
}

bls12_381::g1 public_key::from_affine_bytes_le(const std::array<uint8_t, 96>& affine_non_montgomery_le) {
   std::optional<bls12_381::g1> g1 =
       bls12_381::g1::fromAffineBytesLE(affine_non_montgomery_le, {.check_valid = true, .to_mont = true});
   FCL_ASSERT(g1);
   return *g1;
}

inline std::array<uint8_t, 96> from_span(std::span<const uint8_t, 96> affine_non_montgomery_le) {
   std::array<uint8_t, 96> r;
   std::ranges::copy(affine_non_montgomery_le, r.begin());
   return r;
}

public_key::public_key(std::span<const uint8_t, 96> affine_non_montgomery_le)
    : _affine_non_montgomery_le(from_span(affine_non_montgomery_le)),
      _jacobian_montgomery_le(from_affine_bytes_le(_affine_non_montgomery_le)) {}

public_key::public_key(const std::string& base64urlstr)
    : _affine_non_montgomery_le(deserialize_base64url(base64urlstr)),
      _jacobian_montgomery_le(from_affine_bytes_le(_affine_non_montgomery_le)) {}

std::string public_key::to_string() const {
   std::string data_str =
      fcl::crypto::bls::detail::serialize_base64url<std::array<uint8_t, 96>>(_affine_non_montgomery_le);
   return config::public_key_prefix + data_str;
}

} // namespace fcl::crypto::bls

namespace fcl::crypto::bls {

void to_variant(const public_key& var, variant& vo) {
   vo = var.to_string();
}

void from_variant(const variant& var, public_key& vo) {
   vo = public_key(var.as_string());
}

} // namespace fcl::crypto::bls
