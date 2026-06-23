module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.tui.exceptions;

export import forge.exceptions;

export namespace forge::tui::exceptions {

enum class code : std::uint16_t {
   initialization_failed = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.tui")

using initialization_failed = forge::exceptions::coded_exception<code, code::initialization_failed>;

} // namespace forge::tui::exceptions
