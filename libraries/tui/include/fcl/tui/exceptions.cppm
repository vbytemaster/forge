module;

#include <cstdint>
#include <fcl/exceptions/macros.hpp>

export module fcl.tui.exceptions;

export import fcl.exceptions;

export namespace fcl::tui::exceptions {

enum class code : std::uint16_t {
   initialization_failed = 1,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.tui")

using initialization_failed = fcl::exceptions::coded_exception<code, code::initialization_failed>;

} // namespace fcl::tui::exceptions
