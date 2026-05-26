module;
#include <array>
#include <cstdint>
#include <span>

export module fcl.crypto.chacha20_poly1305;

import fcl.crypto.types;

export namespace fcl::crypto::chacha20_poly1305 {

using key = std::array<std::uint8_t, 32>;
using nonce = std::array<std::uint8_t, 12>;

[[nodiscard]] bytes encrypt(const key& key, const nonce& nonce, std::span<const std::uint8_t> associated_data,
                            std::span<const std::uint8_t> plaintext);

[[nodiscard]] bytes decrypt(const key& key, const nonce& nonce, std::span<const std::uint8_t> associated_data,
                            std::span<const std::uint8_t> ciphertext_and_tag);

} // namespace fcl::crypto::chacha20_poly1305
