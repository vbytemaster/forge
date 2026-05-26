module;
#include <fcl/exception/macros.hpp>
#include <algorithm>
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <cstdint>
#include <exception>
#include <ranges>
#include <span>
#include <string>

module fcl.crypto.bls;

import fcl.exception.exception;

namespace fcl::crypto::bls {

bls12_381::g2 signature::to_jacobian_montgomery_le(const std::array<uint8_t, 192>& affine_non_montgomery_le) {
   auto g2 = bls12_381::g2::fromAffineBytesLE(affine_non_montgomery_le, {.check_valid = true, .to_mont = true});
   FCL_ASSERT(g2, "Invalid signature");
   return *g2;
}

inline std::array<uint8_t, 192> from_span(std::span<const uint8_t, 192> affine_non_montgomery_le) {
   std::array<uint8_t, 192> r;
   std::ranges::copy(affine_non_montgomery_le, r.begin());
   return r;
}

signature::signature(std::span<const uint8_t, 192> affine_non_montgomery_le)
    : _affine_non_montgomery_le(from_span(affine_non_montgomery_le)),
      _jacobian_montgomery_le(to_jacobian_montgomery_le(_affine_non_montgomery_le)) {}

static std::array<uint8_t, 192> sig_parse_base64url(const std::string& base64urlstr) {
   try {
      auto res =
          std::mismatch(config::signature_prefix.begin(), config::signature_prefix.end(), base64urlstr.begin());
      FCL_ASSERT(res.first == config::signature_prefix.end(), "BLS Signature has invalid format : ${str}",
                 fcl::exception::ctx("str", base64urlstr));
      auto data_str = base64urlstr.substr(config::signature_prefix.size());
      return fcl::crypto::bls::detail::deserialize_base64url<std::array<uint8_t, 192>>(data_str);
   }
   FCL_CAPTURE_AND_RETHROW("error parsing signature", fcl::exception::ctx("str", base64urlstr))
}

signature::signature(const std::string& base64urlstr)
    : _affine_non_montgomery_le(sig_parse_base64url(base64urlstr)),
      _jacobian_montgomery_le(to_jacobian_montgomery_le(_affine_non_montgomery_le)) {}

std::string signature::to_string() const {
   std::string data_str =
      fcl::crypto::bls::detail::serialize_base64url<std::array<uint8_t, 192>>(_affine_non_montgomery_le);
   return config::signature_prefix + data_str;
}

aggregate_signature::aggregate_signature(const std::string& base64urlstr)
    : _jacobian_montgomery_le(signature::to_jacobian_montgomery_le(sig_parse_base64url(base64urlstr))) {}

std::string aggregate_signature::to_string() const {
   std::array<uint8_t, 192> affine_non_montgomery_le =
       _jacobian_montgomery_le.toAffineBytesLE(bls12_381::from_mont::yes);
   std::string data_str =
      fcl::crypto::bls::detail::serialize_base64url<std::array<uint8_t, 192>>(affine_non_montgomery_le);
   return config::signature_prefix + data_str;
}

} // namespace fcl::crypto::bls

namespace fcl::crypto::bls {

void to_variant(const signature& var, variant& vo) {
   vo = var.to_string();
}
void from_variant(const variant& var, signature& vo) {
   vo = signature(var.as_string());
}

void to_variant(const aggregate_signature& var, variant& vo) {
   vo = var.to_string();
}
void from_variant(const variant& var, aggregate_signature& vo) {
   vo = aggregate_signature(var.as_string());
}
} // namespace fcl::crypto::bls
