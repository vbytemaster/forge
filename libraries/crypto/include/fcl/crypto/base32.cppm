module;
#include <fcl/exception/macros.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

export module fcl.crypto.base32;

export import fcl.exception.exception;
import fcl.crypto.types;

export namespace fcl::crypto::base32::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.base32")

using invalid_options = fcl::exception::coded_exception<code, code::invalid_options>;

} // namespace fcl::crypto::base32::exceptions

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
