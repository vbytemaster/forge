module;
#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.crypto.digest;

export import forge.exceptions;

export namespace forge::crypto::digest::exceptions {

enum class code : std::uint16_t {
   invalid_size = 1,
   backend_error = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.digest")

using invalid_size = forge::exceptions::coded_exception<code, code::invalid_size>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace forge::crypto::digest::exceptions
