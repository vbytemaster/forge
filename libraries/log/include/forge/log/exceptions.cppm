module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.log.exceptions;

export import forge.exceptions;

export namespace forge::log::exceptions {

enum class code : std::uint16_t {
   invalid_config = 1,
   io_error = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.log")

using invalid_config = forge::exceptions::coded_exception<code, code::invalid_config>;
using io_error = forge::exceptions::coded_exception<code, code::io_error>;

} // namespace forge::log::exceptions
