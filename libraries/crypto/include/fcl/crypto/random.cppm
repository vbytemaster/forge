module;

#include <fcl/exception/macros.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

export module fcl.crypto.random;

export import fcl.exception.exception;
import fcl.crypto.types;

export namespace fcl::crypto::random::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   backend_error = 2,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.random")

using invalid_options = fcl::exception::coded_exception<code, code::invalid_options>;
using backend_error = fcl::exception::coded_exception<code, code::backend_error>;

} // namespace fcl::crypto::random::exceptions

export namespace fcl::crypto {

void fill_random(std::span<std::uint8_t> out);

[[nodiscard]] bytes random_bytes(std::size_t size);

template <std::size_t Size> [[nodiscard]] std::array<std::uint8_t, Size> random_array() {
   auto out = std::array<std::uint8_t, Size>{};
   fill_random(out);
   return out;
}

} // namespace fcl::crypto
