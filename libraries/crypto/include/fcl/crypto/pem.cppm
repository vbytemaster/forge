module;

#include <cstdint>
#include <fcl/exceptions/macros.hpp>
#include <string_view>

export module fcl.crypto.pem;

import fcl.crypto.asymmetric;
import fcl.crypto.types;
export import fcl.exceptions;

export namespace fcl::crypto::pem {

namespace exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   backend_error = 2,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.pem")

using invalid_key = fcl::exceptions::coded_exception<code, code::invalid_key>;
using backend_error = fcl::exceptions::coded_exception<code, code::backend_error>;

} // namespace exceptions

[[nodiscard]] bytes read_block(std::string_view text, std::string_view label);
[[nodiscard]] asymmetric::private_key read_private_key(std::string_view text);
[[nodiscard]] asymmetric::public_key read_public_key(std::string_view text);

} // namespace fcl::crypto::pem
