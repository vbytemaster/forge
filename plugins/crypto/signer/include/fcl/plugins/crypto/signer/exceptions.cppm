module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>

export module fcl.plugins.crypto.signer.exceptions;

import fcl.exceptions;

export namespace fcl::plugins::crypto::signer {

class exceptions {
 public:
   enum class code : std::uint16_t {
      invalid_config = 1,
      key_not_found = 2,
      purpose_denied = 3,
      unsupported_algorithm = 4,
      unsupported_profile = 5,
      invalid_key = 6,
      signing_failed = 7,
   };

   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using key_not_found = fcl::exceptions::coded_exception<code, code::key_not_found>;
   using purpose_denied = fcl::exceptions::coded_exception<code, code::purpose_denied>;
   using unsupported_algorithm = fcl::exceptions::coded_exception<code, code::unsupported_algorithm>;
   using unsupported_profile = fcl::exceptions::coded_exception<code, code::unsupported_profile>;
   using invalid_key = fcl::exceptions::coded_exception<code, code::invalid_key>;
   using signing_failed = fcl::exceptions::coded_exception<code, code::signing_failed>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "fcl.plugins.crypto.signer")

} // namespace fcl::plugins::crypto::signer
