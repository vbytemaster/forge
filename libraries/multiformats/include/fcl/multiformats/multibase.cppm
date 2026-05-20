module;
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

export module fcl.multiformats.multibase;

import fcl.multiformats.types;

export namespace fcl::multiformats {

enum class multibase_code {
   base58btc,
   base32,
   base32_upper,
};

struct decoded_multibase {
   multibase_code code;
   bytes bytes;
};

[[nodiscard]] std::string multibase_encode(multibase_code code, std::span<const std::uint8_t> data);
[[nodiscard]] decoded_multibase multibase_decode(std::string_view value);

} // namespace fcl::multiformats
