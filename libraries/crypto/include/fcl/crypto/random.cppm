module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

export module fcl.crypto.random;

import fcl.crypto.types;

export namespace fcl::crypto {

void fill_random(std::span<std::uint8_t> out);

[[nodiscard]] bytes random_bytes(std::size_t size);
[[nodiscard]] aes256_key generate_key();
[[nodiscard]] aes256_key generate_aes256_key();

template<std::size_t Size>
[[nodiscard]] std::array<std::uint8_t, Size> random_array()
{
   auto out = std::array<std::uint8_t, Size>{};
   fill_random(out);
   return out;
}

} // namespace fcl::crypto
