module;

#include <cstdint>
#include <fcl/exceptions/macros.hpp>

export module fcl.variant.exceptions;

export import fcl.exceptions;

export namespace fcl::variant_exceptions {

enum class code : std::uint16_t {
   invalid_type = 1,
   invalid_operation = 2,
   decode_error = 3,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.variant")

using invalid_type = fcl::exceptions::coded_exception<code, code::invalid_type>;
using invalid_operation = fcl::exceptions::coded_exception<code, code::invalid_operation>;
using decode_error = fcl::exceptions::coded_exception<code, code::decode_error>;

} // namespace fcl::variant_exceptions
