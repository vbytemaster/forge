module;

#include <cstdint>
#include <fcl/exceptions/macros.hpp>

export module fcl.app.exceptions;

export import fcl.exceptions;

export namespace fcl::app::exceptions {

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

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.app")

using invalid_state = fcl::exceptions::coded_exception<code, code::invalid_state>;
using config_failed = fcl::exceptions::coded_exception<code, code::config_failed>;
using plugin_dependency_missing = fcl::exceptions::coded_exception<code, code::plugin_dependency_missing>;
using api_missing = fcl::exceptions::coded_exception<code, code::api_missing>;
using api_version_mismatch = fcl::exceptions::coded_exception<code, code::api_version_mismatch>;
using startup_failed = fcl::exceptions::coded_exception<code, code::startup_failed>;
using shutdown_failed = fcl::exceptions::coded_exception<code, code::shutdown_failed>;
using initialize_failed = fcl::exceptions::coded_exception<code, code::initialize_failed>;

} // namespace fcl::app::exceptions
