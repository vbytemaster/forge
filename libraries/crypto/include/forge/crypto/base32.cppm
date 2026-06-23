module;
#include <forge/exceptions/macros.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

export module forge.crypto.base32;

export import forge.exceptions;
import forge.crypto.types;

export namespace forge::crypto::base32::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.base32")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;

} // namespace forge::crypto::base32::exceptions

export namespace forge::crypto {

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

} // namespace forge::crypto
