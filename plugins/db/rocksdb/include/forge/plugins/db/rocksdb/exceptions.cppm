module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.plugins.db.rocksdb.exceptions;

import forge.exceptions;
import forge.rocksdb.exceptions;

export namespace forge::plugins::db::rocksdb {

namespace backend = forge::rocksdb;

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
   using invalid_argument = backend::exceptions::invalid_argument;
   using corruption = backend::exceptions::corruption;
   using io_error = backend::exceptions::io_error;
   using timed_out = backend::exceptions::timed_out;
   using busy = backend::exceptions::busy;
   using internal_error = backend::exceptions::internal_error;
};

FORGE_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "forge.plugins.db.rocksdb")

} // namespace forge::plugins::db::rocksdb
