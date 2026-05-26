module;

#include <cstdint>
#include <span>

export module fcl.crypto.der;

import fcl.crypto.private_key;
import fcl.crypto.public_key;
import fcl.crypto.types;

export namespace fcl::crypto::der {

[[nodiscard]] public_key read_public_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] private_key read_private_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] bytes write_public_key(const public_key& key);

} // namespace fcl::crypto::der
