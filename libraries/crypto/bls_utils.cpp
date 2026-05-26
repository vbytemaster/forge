module;
#include <bls12-381/bls12-381.hpp>
#include <cstdint>
#include <span>

module fcl.crypto.bls;


namespace fcl::crypto::bls {

bool verify(const public_key& pubkey, std::span<const uint8_t> message, const signature& signature) {
   return bls12_381::verify(pubkey.jacobian_montgomery_le(), message, signature.jacobian_montgomery_le());
};

} // namespace fcl::crypto::bls
