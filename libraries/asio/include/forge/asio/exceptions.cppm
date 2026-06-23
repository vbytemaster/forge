module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.asio.exceptions;

export import forge.exceptions;

export namespace forge::asio::exceptions {

enum class code : std::uint16_t {
   invalid_state = 1,
   invalid_options = 2,
   canceled = 3,
   rejected = 4,
   internal = 5,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.asio")

using invalid_state = forge::exceptions::coded_exception<code, code::invalid_state>;
using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using canceled = forge::exceptions::coded_exception<code, code::canceled>;
using rejected = forge::exceptions::coded_exception<code, code::rejected>;
using internal = forge::exceptions::coded_exception<code, code::internal>;

} // namespace forge::asio::exceptions
