module;

#include <cstdint>
#include <fcl/exceptions/macros.hpp>

export module fcl.multiformats.exceptions;

export import fcl.exceptions;

export namespace fcl::multiformats::exceptions {

enum class code : std::uint16_t {
   invalid_format = 1,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.multiformats")

using invalid_format = fcl::exceptions::coded_exception<code, code::invalid_format>;

} // namespace fcl::multiformats::exceptions
