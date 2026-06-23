module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>
#include <string_view>

export module forge.crypto.pem;

import forge.crypto.asymmetric;
import forge.crypto.types;
export import forge.exceptions;

export namespace forge::crypto::pem {

namespace exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   backend_error = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.pem")

using invalid_key = forge::exceptions::coded_exception<code, code::invalid_key>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace exceptions

[[nodiscard]] bytes read_block(std::string_view text, std::string_view label);
[[nodiscard]] asymmetric::private_key read_private_key(std::string_view text);
[[nodiscard]] asymmetric::public_key read_public_key(std::string_view text);

} // namespace forge::crypto::pem
