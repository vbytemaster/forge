module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>

export module fcl.plugins.http_server.exceptions;

import fcl.exceptions;

export namespace fcl::plugins::http_server {

class exceptions {
 public:
   enum class code : std::uint16_t {
      plugin_not_initialized = 1,
      invalid_config = 2,
      route_conflict = 3,
      publication_closed = 4,
      duplicate_middleware = 5,
   };

   using plugin_not_initialized = fcl::exceptions::coded_exception<code, code::plugin_not_initialized>;
   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using route_conflict = fcl::exceptions::coded_exception<code, code::route_conflict>;
   using publication_closed = fcl::exceptions::coded_exception<code, code::publication_closed>;
   using duplicate_middleware = fcl::exceptions::coded_exception<code, code::duplicate_middleware>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "fcl.plugins.http_server")

} // namespace fcl::plugins::http_server
