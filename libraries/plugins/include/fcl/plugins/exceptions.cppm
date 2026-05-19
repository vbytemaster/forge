module;

#include <cstdint>
#include <fcl/exception/macros.hpp>

export module fcl.plugins.exceptions;

export import fcl.exception.exception;

export namespace fcl::plugins::exceptions {

enum class code : std::uint16_t {
   plugin_not_initialized = 1,
   route_conflict = 2,
   outbox_required = 3,
   outbox_unavailable = 4,
   delivery_queue_full = 5,
   delivery_expired = 6,
   delivery_cancelled = 7,
   relay_policy_denied = 8,
   no_delivery_path = 9,
   invalid_delivery_policy = 10,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.plugins")

using plugin_not_initialized = fcl::exception::coded_exception<code, code::plugin_not_initialized>;
using route_conflict = fcl::exception::coded_exception<code, code::route_conflict>;
using outbox_required = fcl::exception::coded_exception<code, code::outbox_required>;
using outbox_unavailable = fcl::exception::coded_exception<code, code::outbox_unavailable>;
using delivery_queue_full = fcl::exception::coded_exception<code, code::delivery_queue_full>;
using delivery_expired = fcl::exception::coded_exception<code, code::delivery_expired>;
using delivery_cancelled = fcl::exception::coded_exception<code, code::delivery_cancelled>;
using relay_policy_denied = fcl::exception::coded_exception<code, code::relay_policy_denied>;
using no_delivery_path = fcl::exception::coded_exception<code, code::no_delivery_path>;
using invalid_delivery_policy = fcl::exception::coded_exception<code, code::invalid_delivery_policy>;

} // namespace fcl::plugins::exceptions
