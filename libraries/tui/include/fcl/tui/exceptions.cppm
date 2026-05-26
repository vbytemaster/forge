module;

#include <cstdint>
#include <fcl/exception/macros.hpp>

export module fcl.tui.exceptions;

export import fcl.exception.exception;

export namespace fcl::tui::exceptions {

enum class code : std::uint16_t {
   initialization_failed = 1,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.tui")

using initialization_failed = fcl::exception::coded_exception<code, code::initialization_failed>;

} // namespace fcl::tui::exceptions
