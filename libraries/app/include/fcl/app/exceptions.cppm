module;

#include <cstdint>
#include <fcl/exception/macros.hpp>

export module fcl.app.exceptions;

export import fcl.exception.exception;

export namespace fcl::app::exceptions {

enum class code : std::uint16_t {
   invalid_state = 1,
   config_failed = 2,
   plugin_dependency_missing = 3,
   api_missing = 4,
   api_version_mismatch = 5,
   startup_failed = 6,
   shutdown_failed = 7,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.app")

using invalid_state = fcl::exception::coded_exception<code, code::invalid_state>;
using config_failed = fcl::exception::coded_exception<code, code::config_failed>;
using plugin_dependency_missing = fcl::exception::coded_exception<code, code::plugin_dependency_missing>;
using api_missing = fcl::exception::coded_exception<code, code::api_missing>;
using api_version_mismatch = fcl::exception::coded_exception<code, code::api_version_mismatch>;
using startup_failed = fcl::exception::coded_exception<code, code::startup_failed>;
using shutdown_failed = fcl::exception::coded_exception<code, code::shutdown_failed>;

} // namespace fcl::app::exceptions
