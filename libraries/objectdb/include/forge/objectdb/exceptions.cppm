module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.objectdb.exceptions;

export import forge.exceptions;

export namespace forge::objectdb::exceptions {

enum class code : std::uint16_t {
   invalid_descriptor = 1,
   invalid_cursor = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.objectdb")

using invalid_descriptor = forge::exceptions::coded_exception<code, code::invalid_descriptor>;
using invalid_cursor = forge::exceptions::coded_exception<code, code::invalid_cursor>;

} // namespace forge::objectdb::exceptions
