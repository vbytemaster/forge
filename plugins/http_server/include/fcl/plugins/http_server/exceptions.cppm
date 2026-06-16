module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>

export module fcl.plugins.http_server.exceptions;

import fcl.exceptions;

export namespace fcl::plugins::http_server {

class exceptions {
 public:
   enum class code : std::uint16_t {
      invalid_config = 1,
      publication_closed = 2,
      startup_failed = 3,
   };

   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using publication_closed = fcl::exceptions::coded_exception<code, code::publication_closed>;
   using startup_failed = fcl::exceptions::coded_exception<code, code::startup_failed>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "fcl.plugins.http_server")

} // namespace fcl::plugins::http_server
