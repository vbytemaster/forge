module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>

export module fcl.plugins.p2p_pubsub.exceptions;

import fcl.exceptions;

export namespace fcl::plugins::p2p_pubsub {

class exceptions {
 public:
   enum class code : std::uint16_t {
      plugin_not_initialized = 1,
      invalid_config = 2,
      topic_not_allowed = 3,
      subscription_not_found = 4,
      handler_limit = 5,
      message_too_large = 6,
   };

   using plugin_not_initialized = fcl::exceptions::coded_exception<code, code::plugin_not_initialized>;
   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using topic_not_allowed = fcl::exceptions::coded_exception<code, code::topic_not_allowed>;
   using subscription_not_found = fcl::exceptions::coded_exception<code, code::subscription_not_found>;
   using handler_limit = fcl::exceptions::coded_exception<code, code::handler_limit>;
   using message_too_large = fcl::exceptions::coded_exception<code, code::message_too_large>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "fcl.plugins.p2p_pubsub")

} // namespace fcl::plugins::p2p_pubsub
