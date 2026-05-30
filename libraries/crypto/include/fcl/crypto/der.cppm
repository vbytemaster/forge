module;

#include <fcl/exception/macros.hpp>
#include <cstdint>
#include <span>

export module fcl.crypto.der;

import fcl.crypto.asymmetric;
import fcl.crypto.types;
export import fcl.exception.exception;

export namespace fcl::crypto::der {

namespace exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   invalid_options = 2,
   backend_error = 3,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.der")

using invalid_key = fcl::exception::coded_exception<code, code::invalid_key>;
using invalid_options = fcl::exception::coded_exception<code, code::invalid_options>;
using backend_error = fcl::exception::coded_exception<code, code::backend_error>;

} // namespace exceptions

[[nodiscard]] asymmetric::public_key read_public_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] asymmetric::private_key read_private_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] bytes write_public_key(const asymmetric::public_key& key);

} // namespace fcl::crypto::der
