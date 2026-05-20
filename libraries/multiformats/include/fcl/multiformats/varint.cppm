module;
#include <cstddef>
#include <cstdint>
#include <span>

export module fcl.multiformats.varint;

import fcl.multiformats.types;

export namespace fcl::multiformats {

struct decoded_varint {
   std::uint64_t value = 0;
   std::size_t size = 0;
};

[[nodiscard]] bytes varint_encode(std::uint64_t value);
[[nodiscard]] decoded_varint varint_decode(std::span<const std::uint8_t> data);

} // namespace fcl::multiformats
