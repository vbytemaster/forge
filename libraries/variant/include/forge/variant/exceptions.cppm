module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.variant.exceptions;

export import forge.exceptions;

export namespace forge::variant_exceptions {

enum class code : std::uint16_t {
   invalid_type = 1,
   invalid_operation = 2,
   decode_error = 3,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.variant")

using invalid_type = forge::exceptions::coded_exception<code, code::invalid_type>;
using invalid_operation = forge::exceptions::coded_exception<code, code::invalid_operation>;
using decode_error = forge::exceptions::coded_exception<code, code::decode_error>;

} // namespace forge::variant_exceptions
