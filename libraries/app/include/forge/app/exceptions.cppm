module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.app.exceptions;

export import forge.exceptions;

export namespace forge::app::exceptions {

enum class code : std::uint16_t {
   invalid_state = 1,
   config_failed = 2,
   plugin_dependency_missing = 3,
   api_missing = 4,
   api_version_mismatch = 5,
   startup_failed = 6,
   shutdown_failed = 7,
   initialize_failed = 8,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.app")

using invalid_state = forge::exceptions::coded_exception<code, code::invalid_state>;
using config_failed = forge::exceptions::coded_exception<code, code::config_failed>;
using plugin_dependency_missing = forge::exceptions::coded_exception<code, code::plugin_dependency_missing>;
using api_missing = forge::exceptions::coded_exception<code, code::api_missing>;
using api_version_mismatch = forge::exceptions::coded_exception<code, code::api_version_mismatch>;
using startup_failed = forge::exceptions::coded_exception<code, code::startup_failed>;
using shutdown_failed = forge::exceptions::coded_exception<code, code::shutdown_failed>;
using initialize_failed = forge::exceptions::coded_exception<code, code::initialize_failed>;

} // namespace forge::app::exceptions
