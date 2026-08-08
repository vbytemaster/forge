module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module forge.chain.protocol.block_query;

import forge.variant.containers;
import forge.variant.described;
export import forge.variant.value;
export import forge.chain.protocol.audit;
export import forge.chain.protocol.block;
export import forge.chain.protocol.blockchain_parameters;
export import forge.chain.protocol.finalizer_policy;
export import forge.chain.protocol.producer_authority;
export import forge.chain.protocol.producer_schedule;

export namespace forge::chain::protocol {

struct block_request {
   std::optional<block_id> id;
   std::optional<std::uint32_t> num;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const block_request&) const = default;
};

struct block_record {
   forge::chain::protocol::signed_block block;
   block_id id;
   std::uint32_t num = 0;
   bool canonical = false;
};

struct block_response : audited_response, block_record {};

struct block_header_response : audited_response {
   forge::chain::protocol::signed_block_header header;
   block_id id;
   std::uint32_t num = 0;
   bool canonical = false;
};

struct block_state_response : audited_response {
   block_id id;
   std::uint32_t num = 0;
   bytes state;

   bool operator==(const block_state_response&) const = default;
};

struct block_range_request {
   std::uint32_t first = 0;
   std::uint32_t limit = 256;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const block_range_request&) const = default;
};

struct block_range_response : audited_response {
   std::vector<block_record> blocks;
   std::optional<std::uint32_t> next;
};

struct protocol_features_request {
   std::optional<std::uint32_t> lower_bound;
   std::optional<std::uint32_t> upper_bound;
   std::uint32_t limit = 256;
   bool search_by_block_num = false;
   bool reverse = false;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const protocol_features_request&) const = default;
};

struct protocol_features_response : audited_response {
   std::vector<forge::variant> features;
   std::optional<std::uint32_t> next;

   bool operator==(const protocol_features_response&) const = default;
};

struct consensus_parameters_response : audited_response {
   forge::chain::protocol::blockchain_parameters parameters;
   std::optional<forge::variant> wasm;
};

struct producers_request {
   bool json = false;
   std::string lower_bound;
   std::uint32_t limit = 50;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const producers_request&) const = default;
};

struct producers_response : audited_response {
   std::vector<forge::variant> rows;
   double total_vote_weight = 0;
   std::string next;

   bool operator==(const producers_response&) const = default;
};

struct producer_schedule_response : audited_response {
   forge::chain::protocol::producer_authority_schedule active;
   std::optional<forge::chain::protocol::producer_authority_schedule> pending;
   std::optional<forge::chain::protocol::producer_authority_schedule> proposed;

   bool operator==(const producer_schedule_response&) const = default;
};

struct finalizer_info_response : audited_response {
   forge::chain::protocol::finalizer_policy active;
   std::optional<forge::chain::protocol::finalizer_policy> pending;
   std::vector<forge::variant> last_votes;

   bool operator==(const finalizer_info_response&) const = default;
};

BOOST_DESCRIBE_STRUCT(block_request, (), (id, num, finality_from, audit))
BOOST_DESCRIBE_STRUCT(block_record, (), (block, id, num, canonical))
BOOST_DESCRIBE_STRUCT(block_response, (audited_response, block_record), ())
BOOST_DESCRIBE_STRUCT(block_header_response, (audited_response), (header, id, num, canonical))
BOOST_DESCRIBE_STRUCT(block_state_response, (audited_response), (id, num, state))
BOOST_DESCRIBE_STRUCT(block_range_request, (), (first, limit, finality_from, audit))
BOOST_DESCRIBE_STRUCT(block_range_response, (audited_response), (blocks, next))
BOOST_DESCRIBE_STRUCT(protocol_features_request, (),
                      (lower_bound, upper_bound, limit, search_by_block_num, reverse, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(protocol_features_response, (audited_response), (features, next))
BOOST_DESCRIBE_STRUCT(consensus_parameters_response, (audited_response), (parameters, wasm))
BOOST_DESCRIBE_STRUCT(producers_request, (), (json, lower_bound, limit, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(producers_response, (audited_response), (rows, total_vote_weight, next))
BOOST_DESCRIBE_STRUCT(producer_schedule_response, (audited_response), (active, pending, proposed))
BOOST_DESCRIBE_STRUCT(finalizer_info_response, (audited_response), (active, pending, last_votes))

} // namespace forge::chain::protocol
