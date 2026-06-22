module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>

export module fcl.plugins.secret.provider.exceptions;

import fcl.exceptions;

export namespace fcl::plugins::secret::provider {

class exceptions {
 public:
   enum class code : std::uint16_t {
      invalid_config = 1,
      secret_not_found = 2,
      purpose_denied = 3,
      operation_denied = 4,
      invalid_secret = 5,
      invalid_source = 6,
      size_limit_exceeded = 7,
      crypto_failed = 8,
   };

   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using secret_not_found = fcl::exceptions::coded_exception<code, code::secret_not_found>;
   using purpose_denied = fcl::exceptions::coded_exception<code, code::purpose_denied>;
   using operation_denied = fcl::exceptions::coded_exception<code, code::operation_denied>;
   using invalid_secret = fcl::exceptions::coded_exception<code, code::invalid_secret>;
   using invalid_source = fcl::exceptions::coded_exception<code, code::invalid_source>;
   using size_limit_exceeded = fcl::exceptions::coded_exception<code, code::size_limit_exceeded>;
   using crypto_failed = fcl::exceptions::coded_exception<code, code::crypto_failed>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "fcl.plugins.secret.provider")

} // namespace fcl::plugins::secret::provider
