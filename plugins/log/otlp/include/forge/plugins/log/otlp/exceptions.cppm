module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.plugins.log.otlp.exceptions;

import forge.exceptions;

export namespace forge::plugins::log::otlp {

class exceptions {
 public:
   enum class code : std::uint16_t {
      invalid_config = 1,
      startup_failed = 2,
      exporter_unavailable = 3,
   };

   using invalid_config = forge::exceptions::coded_exception<code, code::invalid_config>;
   using startup_failed = forge::exceptions::coded_exception<code, code::startup_failed>;
   using exporter_unavailable = forge::exceptions::coded_exception<code, code::exporter_unavailable>;
};

FORGE_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "forge.plugins.log.otlp")

} // namespace forge::plugins::log::otlp
