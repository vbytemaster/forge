module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.schema.exceptions;

export import forge.exceptions;

export namespace forge::schema::exceptions {

enum class code : std::uint16_t {
   invalid_value = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.schema")

using invalid_value = forge::exceptions::coded_exception<code, code::invalid_value>;

} // namespace forge::schema::exceptions
