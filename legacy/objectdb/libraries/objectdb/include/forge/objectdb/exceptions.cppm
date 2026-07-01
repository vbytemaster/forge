module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.objectdb.exceptions;

import forge.exceptions;

export namespace forge::objectdb {

class exceptions {
 public:
   enum class code : std::uint16_t {
      invalid_key = 1,
      invalid_cursor = 2,
      invalid_descriptor = 3,
   };

   using invalid_key = forge::exceptions::coded_exception<code, code::invalid_key>;
   using invalid_cursor = forge::exceptions::coded_exception<code, code::invalid_cursor>;
   using invalid_descriptor = forge::exceptions::coded_exception<code, code::invalid_descriptor>;
};

FORGE_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "forge.objectdb")

} // namespace forge::objectdb
