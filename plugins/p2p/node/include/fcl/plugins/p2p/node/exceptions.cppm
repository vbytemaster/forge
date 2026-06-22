module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>

export module fcl.plugins.p2p.node.exceptions;

import fcl.exceptions;

export namespace fcl::plugins::p2p::node {

class exceptions {
 public:
   enum class code : std::uint16_t {
      plugin_not_initialized = 1,
      route_conflict = 2,
      invalid_config = 3,
   };

   using plugin_not_initialized = fcl::exceptions::coded_exception<code, code::plugin_not_initialized>;
   using route_conflict = fcl::exceptions::coded_exception<code, code::route_conflict>;
   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "fcl.plugins.p2p.node")

} // namespace fcl::plugins::p2p::node
