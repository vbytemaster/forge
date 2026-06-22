module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>

export module fcl.plugins.p2p.diagnostics.exceptions;

import fcl.exceptions;

export namespace fcl::plugins::p2p::diagnostics {

class exceptions {
 public:
   enum class code : std::uint16_t {
      plugin_not_initialized = 1,
      invalid_config = 2,
      not_found = 3,
   };

   using plugin_not_initialized = fcl::exceptions::coded_exception<code, code::plugin_not_initialized>;
   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using not_found = fcl::exceptions::coded_exception<code, code::not_found>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "fcl.plugins.p2p.diagnostics")

} // namespace fcl::plugins::p2p::diagnostics
