module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>

export module fcl.plugins.p2p_api_resolver.exceptions;

import fcl.exceptions;

export namespace fcl::plugins::p2p_api_resolver {

class exceptions {
 public:
   enum class code : std::uint16_t {
      plugin_not_initialized = 1,
      invalid_config = 2,
      duplicate_api = 3,
      not_found = 4,
      incompatible_api = 5,
      protocol_error = 6,
   };

   using plugin_not_initialized = fcl::exceptions::coded_exception<code, code::plugin_not_initialized>;
   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using duplicate_api = fcl::exceptions::coded_exception<code, code::duplicate_api>;
   using not_found = fcl::exceptions::coded_exception<code, code::not_found>;
   using incompatible_api = fcl::exceptions::coded_exception<code, code::incompatible_api>;
   using protocol_error = fcl::exceptions::coded_exception<code, code::protocol_error>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "fcl.plugins.p2p_api_resolver")

} // namespace fcl::plugins::p2p_api_resolver
