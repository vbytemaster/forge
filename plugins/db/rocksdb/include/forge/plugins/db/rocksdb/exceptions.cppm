module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.plugins.db.rocksdb.exceptions;

import forge.exceptions;

export namespace forge::plugins::db::rocksdb {

class exceptions {
 public:
   enum class code : std::uint16_t {
      invalid_config = 1,
      stopped = 2,
      invalid_argument = 3,
      corruption = 4,
      io_error = 5,
      timed_out = 6,
      busy = 7,
      internal_error = 255,
   };

   using invalid_config = forge::exceptions::coded_exception<code, code::invalid_config>;
   using stopped = forge::exceptions::coded_exception<code, code::stopped>;
   using invalid_argument = forge::exceptions::coded_exception<code, code::invalid_argument>;
   using corruption = forge::exceptions::coded_exception<code, code::corruption>;
   using io_error = forge::exceptions::coded_exception<code, code::io_error>;
   using timed_out = forge::exceptions::coded_exception<code, code::timed_out>;
   using busy = forge::exceptions::coded_exception<code, code::busy>;
   using internal_error = forge::exceptions::coded_exception<code, code::internal_error>;
};

FORGE_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "forge.plugins.db.rocksdb")

} // namespace forge::plugins::db::rocksdb
