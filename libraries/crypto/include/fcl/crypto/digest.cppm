module;
#include <cstdint>
#include <fcl/exceptions/macros.hpp>

export module fcl.crypto.digest;

export import fcl.exceptions;

export namespace fcl::crypto::digest::exceptions {

enum class code : std::uint16_t {
   invalid_size = 1,
   backend_error = 2,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.digest")

using invalid_size = fcl::exceptions::coded_exception<code, code::invalid_size>;
using backend_error = fcl::exceptions::coded_exception<code, code::backend_error>;

} // namespace fcl::crypto::digest::exceptions
