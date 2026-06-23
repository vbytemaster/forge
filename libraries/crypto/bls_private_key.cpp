module;
#include <forge/exceptions/macros.hpp>
#include <algorithm>
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

module forge.crypto.bls;

import forge.core.utility;
import forge.crypto.random;
import forge.exceptions;

namespace forge::crypto::bls {

using from_mont = bls12_381::from_mont;

public_key private_key::get_public_key() const {
   bls12_381::g1 pk = bls12_381::public_key(_sk);
   return public_key(pk.toAffineBytesLE(from_mont::yes));
}

signature private_key::proof_of_possession() const {
   bls12_381::g2 proof = bls12_381::pop_prove(_sk);
   return signature(proof.toAffineBytesLE(from_mont::yes));
}

signature private_key::sign(std::span<const uint8_t> message) const {
   bls12_381::g2 sig = bls12_381::sign(_sk, message);
   return signature(sig.toAffineBytesLE(from_mont::yes));
}

private_key private_key::generate() {
   auto v = forge::crypto::random_bytes(32);
   return private_key(v);
}

static std::array<uint64_t, 4> priv_parse_base64url(const std::string& base64urlstr) {
   auto res = std::mismatch(config::private_key_prefix.begin(), config::private_key_prefix.end(),
                            base64urlstr.begin());
   FORGE_ASSERT(res.first == config::private_key_prefix.end(), "BLS Private Key has invalid format : ${str}",
              forge::exceptions::ctx("str", base64urlstr));

   auto data_str = base64urlstr.substr(config::private_key_prefix.size());

   std::array<uint64_t, 4> bytes =
      forge::crypto::bls::detail::deserialize_base64url<std::array<uint64_t, 4>>(data_str);

   return bytes;
}

private_key::private_key(const std::string& base64urlstr) : _sk(priv_parse_base64url(base64urlstr)) {}

std::string private_key::to_string() const {
   std::string data_str = forge::crypto::bls::detail::serialize_base64url<std::array<uint64_t, 4>>(_sk);

   return config::private_key_prefix + data_str;
}

bool operator==(const private_key& pk1, const private_key& pk2) {
   return pk1._sk == pk2._sk;
}

} // namespace forge::crypto::bls

namespace forge::crypto::bls {
void to_variant(const private_key& var, variant& vo) {
   vo = var.to_string();
}

void from_variant(const variant& var, private_key& vo) {
   vo = private_key(var.as_string());
}

} // namespace forge::crypto::bls
