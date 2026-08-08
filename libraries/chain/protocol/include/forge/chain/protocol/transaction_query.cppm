module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module forge.chain.protocol.transaction_query;

export import forge.chain.protocol.audit;
export import forge.chain.protocol.block;
export import forge.chain.protocol.time;
export import forge.chain.protocol.transaction;
export import forge.chain.protocol.transaction_trace;

export namespace forge::chain::protocol {

enum class transaction_lifecycle : std::uint8_t {
   accepted = 0,
   included = 1,
   finalized = 2,
   forked_out = 3,
   rejected = 4,
   expired = 5,
   unknown = 6,
};

enum class transaction_execution_status : std::uint8_t {
   executed = 0,
   rejected = 1,
};

struct transaction_submit_request {
   forge::chain::protocol::packed_transaction transaction;
   bool return_failure_trace = true;
   bool retry = false;
   std::optional<std::uint16_t> retry_blocks;
   std::uint64_t timeout_ms = 30'000;
};

struct transaction_submit_batch_request {
   std::vector<transaction_submit_request> transactions;
   // Total budget; owners cap each item deadline by the remaining batch time.
   std::uint64_t timeout_ms = 30'000;
};

struct transaction_submit_response {
   forge::chain::protocol::transaction_id id;
   transaction_lifecycle state = transaction_lifecycle::accepted;
   std::optional<transaction_trace> trace;

   bool operator==(const transaction_submit_response&) const = default;
};

struct transaction_status_request {
   forge::chain::protocol::transaction_id id;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const transaction_status_request&) const = default;
};

struct transaction_status_response : audited_response {
   forge::chain::protocol::transaction_id id;
   transaction_lifecycle state = transaction_lifecycle::unknown;
   std::optional<block_id> block;
   std::optional<std::uint32_t> block_num;
   std::optional<forge::chain::protocol::time_point> block_time;
   std::optional<forge::chain::protocol::time_point> expiration;
   block_id head;
   std::uint32_t head_num = 0;
   forge::chain::protocol::time_point head_time{};
   block_id finalized;
   std::uint32_t finalized_num = 0;
   forge::chain::protocol::time_point finalized_time{};
   block_id earliest_tracked;
   std::uint32_t earliest_tracked_num = 0;
   std::optional<forge::chain::protocol::transaction_receipt> receipt;
   std::optional<transaction_trace> trace;
};

struct transaction_await_request {
   forge::chain::protocol::transaction_id id;
   transaction_lifecycle desired = transaction_lifecycle::finalized;
   std::uint64_t timeout_ms = 30'000;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const transaction_await_request&) const = default;
};

struct transaction_required_keys_request {
   forge::chain::protocol::transaction transaction;
   std::vector<forge::chain::protocol::public_key> available;
};

struct transaction_read_only_request {
   forge::chain::protocol::packed_transaction transaction;
   bool return_failure_trace = true;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;
};

struct transaction_read_only_response : audited_response {
   forge::chain::protocol::transaction_id id;
   transaction_execution_status status = transaction_execution_status::executed;
   transaction_trace trace;

   bool operator==(const transaction_read_only_response&) const = default;
};

BOOST_DESCRIBE_ENUM(transaction_lifecycle, accepted, included, finalized, forked_out, rejected, expired, unknown)
BOOST_DESCRIBE_ENUM(transaction_execution_status, executed, rejected)
BOOST_DESCRIBE_STRUCT(transaction_submit_request, (),
                      (transaction, return_failure_trace, retry, retry_blocks, timeout_ms))
BOOST_DESCRIBE_STRUCT(transaction_submit_batch_request, (), (transactions, timeout_ms))
BOOST_DESCRIBE_STRUCT(transaction_submit_response, (), (id, state, trace))
BOOST_DESCRIBE_STRUCT(transaction_status_request, (), (id, finality_from, audit))
BOOST_DESCRIBE_STRUCT(transaction_status_response, (audited_response),
                      (id, state, block, block_num, block_time, expiration, head, head_num, head_time, finalized,
                       finalized_num, finalized_time, earliest_tracked, earliest_tracked_num, receipt, trace))
BOOST_DESCRIBE_STRUCT(transaction_await_request, (), (id, desired, timeout_ms, finality_from, audit))
BOOST_DESCRIBE_STRUCT(transaction_required_keys_request, (), (transaction, available))
BOOST_DESCRIBE_STRUCT(transaction_read_only_request, (),
                      (transaction, return_failure_trace, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(transaction_read_only_response, (audited_response), (id, status, trace))

} // namespace forge::chain::protocol
