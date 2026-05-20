module;
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

export module fcl.crypto.base32;

import fcl.crypto.types;

export namespace fcl::crypto {

enum class base32_case {
   lower,
   upper,
};

struct base32_options {
   base32_case alphabet_case = base32_case::lower;
   bool padding = false;
};

[[nodiscard]] std::string base32_encode(std::span<const std::uint8_t> data, base32_options options = {});
[[nodiscard]] bytes base32_decode(std::string_view value);

} // namespace fcl::crypto
