module;

#include <cstdint>
#include <fcl/exceptions/macros.hpp>

export module fcl.raw.exceptions;

export import fcl.exceptions;

export namespace fcl::raw::exceptions {

enum class code : std::uint16_t {
   range_error = 1,
   codec_error = 2,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.raw")

using range_error = fcl::exceptions::coded_exception<code, code::range_error>;
using codec_error = fcl::exceptions::coded_exception<code, code::codec_error>;

} // namespace fcl::raw::exceptions
