module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.otlp.exceptions;

export import forge.exceptions;

export namespace forge::otlp::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   queue_closed = 2,
   export_failed = 3,
   spool_error = 4,
   capture_active = 5,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.otlp")

using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using queue_closed = forge::exceptions::coded_exception<code, code::queue_closed>;
using export_failed = forge::exceptions::coded_exception<code, code::export_failed>;
using spool_error = forge::exceptions::coded_exception<code, code::spool_error>;
using capture_active = forge::exceptions::coded_exception<code, code::capture_active>;

} // namespace forge::otlp::exceptions
