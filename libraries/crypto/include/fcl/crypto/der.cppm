module;

#include <cstdint>
#include <span>

export module fcl.crypto.der;

import fcl.crypto.asymmetric;
import fcl.crypto.types;

export namespace fcl::crypto::der {

[[nodiscard]] asymmetric::public_key read_public_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] asymmetric::private_key read_private_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] bytes write_public_key(const asymmetric::public_key& key);

} // namespace fcl::crypto::der
