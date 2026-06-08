#pragma once

#include <cstdint>
#include <span>
#include <vector>

extern "C++" {
namespace fcl::p2p {

[[nodiscard]] public_key public_key_from_crypto(const fcl::crypto::asymmetric::public_key& key);
[[nodiscard]] fcl::crypto::asymmetric::public_key crypto_public_key(const public_key& key);
[[nodiscard]] std::vector<std::uint8_t> sign_identity(const fcl::crypto::asymmetric::private_key& key,
                                                      std::span<const std::uint8_t> message);
[[nodiscard]] bool verify_identity_signature(const public_key& key, std::span<const std::uint8_t> message,
                                             std::span<const std::uint8_t> signature);

} // namespace fcl::p2p
}
