module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.rocksdb.exceptions;

import forge.exceptions;

export namespace forge::rocksdb {

class exceptions {
 public:
   enum class code : std::uint16_t {
      invalid_argument = 1,
      corruption = 2,
      io_error = 3,
      timed_out = 4,
      busy = 5,
      internal_error = 255,
   };

   using invalid_argument = forge::exceptions::coded_exception<code, code::invalid_argument>;
   using corruption = forge::exceptions::coded_exception<code, code::corruption>;
   using io_error = forge::exceptions::coded_exception<code, code::io_error>;
   using timed_out = forge::exceptions::coded_exception<code, code::timed_out>;
   using busy = forge::exceptions::coded_exception<code, code::busy>;
   using internal_error = forge::exceptions::coded_exception<code, code::internal_error>;
};

FORGE_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "forge.rocksdb")

} // namespace forge::rocksdb
