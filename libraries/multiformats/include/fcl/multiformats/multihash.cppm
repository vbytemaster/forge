module;
#include <cstdint>
#include <span>
#include <string>

export module fcl.multiformats.multihash;

import fcl.multiformats.types;
import fcl.multiformats.multicodec;

export namespace fcl::multiformats {

struct multihash {
   std::uint64_t code = 0;
   bytes digest;

   [[nodiscard]] bytes encode() const;
   [[nodiscard]] std::string digest_hex() const;

   [[nodiscard]] static multihash decode(std::span<const std::uint8_t> data);
   [[nodiscard]] static multihash identity(std::span<const std::uint8_t> data);
   [[nodiscard]] static multihash sha2_256(std::span<const std::uint8_t> data);
   [[nodiscard]] static multihash sha2_512(std::span<const std::uint8_t> data);
};

} // namespace fcl::multiformats
