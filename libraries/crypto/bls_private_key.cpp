module;
#include <fcl/exception/macros.hpp>
#include <algorithm>
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

module fcl.crypto.bls_private_key;

import fcl.core.utility;
import fcl.crypto.bls_common;
import fcl.crypto.bls_public_key;
import fcl.crypto.bls_signature;
import fcl.crypto.common;
import fcl.crypto.rand;
import fcl.exception.exception;

namespace fcl::crypto::blslib {

using from_mont = bls12_381::from_mont;

bls_public_key bls_private_key::get_public_key() const {
   bls12_381::g1 pk = bls12_381::public_key(_sk);
   return bls_public_key(pk.toAffineBytesLE(from_mont::yes));
}

bls_signature bls_private_key::proof_of_possession() const {
   bls12_381::g2 proof = bls12_381::pop_prove(_sk);
   return bls_signature(proof.toAffineBytesLE(from_mont::yes));
}

bls_signature bls_private_key::sign(std::span<const uint8_t> message) const {
   bls12_381::g2 sig = bls12_381::sign(_sk, message);
   return bls_signature(sig.toAffineBytesLE(from_mont::yes));
}

bls_private_key bls_private_key::generate() {
   std::vector<uint8_t> v(32);
   rand_bytes(reinterpret_cast<char*>(&v[0]), 32);
   return bls_private_key(v);
}

static std::array<uint64_t, 4> priv_parse_base64url(const std::string& base64urlstr) {
   auto res = std::mismatch(config::bls_private_key_prefix.begin(), config::bls_private_key_prefix.end(),
                            base64urlstr.begin());
   FCL_ASSERT(res.first == config::bls_private_key_prefix.end(), "BLS Private Key has invalid format : ${str}",
              fcl::exception::ctx("str", base64urlstr));

   auto data_str = base64urlstr.substr(config::bls_private_key_prefix.size());

   std::array<uint64_t, 4> bytes = fcl::crypto::blslib::deserialize_base64url<std::array<uint64_t, 4>>(data_str);

   return bytes;
}

bls_private_key::bls_private_key(const std::string& base64urlstr) : _sk(priv_parse_base64url(base64urlstr)) {}

std::string bls_private_key::to_string() const {
   std::string data_str = fcl::crypto::blslib::serialize_base64url<std::array<uint64_t, 4>>(_sk);

   return config::bls_private_key_prefix + data_str;
}

bool operator==(const bls_private_key& pk1, const bls_private_key& pk2) {
   return pk1._sk == pk2._sk;
}

} // namespace fcl::crypto::blslib

namespace fcl::crypto::blslib {
void to_variant(const bls_private_key& var, variant& vo) {
   vo = var.to_string();
}

void from_variant(const variant& var, bls_private_key& vo) {
   vo = bls_private_key(var.as_string());
}

} // namespace fcl::crypto::blslib
