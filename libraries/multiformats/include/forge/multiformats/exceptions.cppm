module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.multiformats.exceptions;

export import forge.exceptions;

export namespace forge::multiformats::exceptions {

enum class code : std::uint16_t {
   invalid_format = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.multiformats")

using invalid_format = forge::exceptions::coded_exception<code, code::invalid_format>;

} // namespace forge::multiformats::exceptions
