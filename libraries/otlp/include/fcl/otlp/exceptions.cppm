module;

#include <cstdint>
#include <fcl/exceptions/macros.hpp>

export module fcl.otlp.exceptions;

export import fcl.exceptions;

export namespace fcl::otlp::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   queue_closed = 2,
   export_failed = 3,
   spool_error = 4,
   capture_active = 5,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.otlp")

using invalid_options = fcl::exceptions::coded_exception<code, code::invalid_options>;
using queue_closed = fcl::exceptions::coded_exception<code, code::queue_closed>;
using export_failed = fcl::exceptions::coded_exception<code, code::export_failed>;
using spool_error = fcl::exceptions::coded_exception<code, code::spool_error>;
using capture_active = fcl::exceptions::coded_exception<code, code::capture_active>;

} // namespace fcl::otlp::exceptions
