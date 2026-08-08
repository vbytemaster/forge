module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module forge.chain.protocol.transaction_trace;

export import forge.chain.protocol.action;
export import forge.chain.protocol.action_receipt;
export import forge.chain.protocol.transaction;

export namespace forge::chain::protocol {

struct transaction_error {
   std::string category;
   std::int32_t code = 0;
   std::string message;

   bool operator==(const transaction_error&) const = default;
};

struct action_trace {
   action action;
   action_receipt receipt;
   bool context_free = false;
   std::uint32_t depth = 0;
   std::string console;

   bool operator==(const action_trace&) const = default;
};

struct transaction_trace {
   transaction_id id;
   std::vector<action_trace> actions;
   std::uint32_t cpu_usage_us = 0;
   std::uint64_t net_usage = 0;
   std::optional<transaction_error> error;

   bool operator==(const transaction_trace&) const = default;
};

BOOST_DESCRIBE_STRUCT(transaction_error, (), (category, code, message))
BOOST_DESCRIBE_STRUCT(action_trace, (), (action, receipt, context_free, depth, console))
BOOST_DESCRIBE_STRUCT(transaction_trace, (), (id, actions, cpu_usage_us, net_usage, error))

} // namespace forge::chain::protocol
