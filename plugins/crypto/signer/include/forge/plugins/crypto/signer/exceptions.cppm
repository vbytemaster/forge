module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.plugins.crypto.signer.exceptions;

import forge.exceptions;

export namespace forge::plugins::crypto::signer {

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

   using invalid_config = forge::exceptions::coded_exception<code, code::invalid_config>;
   using key_not_found = forge::exceptions::coded_exception<code, code::key_not_found>;
   using purpose_denied = forge::exceptions::coded_exception<code, code::purpose_denied>;
   using unsupported_algorithm = forge::exceptions::coded_exception<code, code::unsupported_algorithm>;
   using unsupported_profile = forge::exceptions::coded_exception<code, code::unsupported_profile>;
   using invalid_key = forge::exceptions::coded_exception<code, code::invalid_key>;
   using signing_failed = forge::exceptions::coded_exception<code, code::signing_failed>;
};

FORGE_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "forge.plugins.crypto.signer")

} // namespace forge::plugins::crypto::signer
