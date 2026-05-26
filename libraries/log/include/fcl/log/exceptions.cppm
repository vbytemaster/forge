module;

#include <cstdint>
#include <fcl/exception/macros.hpp>

export module fcl.log.exceptions;

export import fcl.exception.exception;

export namespace fcl::log::exceptions {

enum class code : std::uint16_t {
   invalid_config = 1,
   io_error = 2,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.log")

using invalid_config = fcl::exception::coded_exception<code, code::invalid_config>;
using io_error = fcl::exception::coded_exception<code, code::io_error>;

} // namespace fcl::log::exceptions
