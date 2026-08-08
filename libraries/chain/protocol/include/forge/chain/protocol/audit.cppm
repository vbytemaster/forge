module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module forge.chain.protocol.audit;

export import forge.chain.protocol.types;

import forge.crypto.digest.sha256;
import forge.chain.core.merkle;
import forge.variant.containers;
import forge.variant.conversion;
import forge.variant.described;
import forge.variant.static_variant;
import forge.variant.value;
import forge.variant.variant_dynamic_bitset;

export namespace forge::chain::protocol {

enum class audit_mode : std::uint8_t {
   none = 0,
   if_available = 1,
   required = 2,
};

enum class audit_class : std::uint8_t {
   none = 0,
   finality = 1,
   state_point = 2,
   state_range = 3,
   state_changes = 4,
   transaction_inclusion = 5,
   deterministic_composite = 6,
   unsupported = 7,
};

struct state_anchor {
   chain_id chain;
   block_id block;
   std::uint32_t block_num = 0;
   digest transaction_root;
   digest state_root;
   std::uint64_t state_size = 0;
   digest change_root;
   std::uint64_t change_count = 0;

   bool operator==(const state_anchor&) const = default;
};

struct anchored_request {
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const anchored_request&) const = default;
};

struct proof_blob {
   std::string scheme;
   std::uint32_t version = 1;
   bytes payload;

   bool operator==(const proof_blob&) const = default;
};

struct content_witness {
   digest hash;
   bytes value;

   bool operator==(const content_witness&) const = default;
};

using merkle_step = forge::chain::core::merkle_step;

struct transaction_inclusion_proof {
   digest leaf;
   std::uint64_t index = 0;
   std::uint64_t leaf_count = 0;
   std::vector<merkle_step> path;

   bool operator==(const transaction_inclusion_proof&) const = default;
};

struct audit_bundle {
   std::optional<proof_blob> finality;
   std::optional<proof_blob> ancestry;
   std::vector<proof_blob> state;
   std::vector<content_witness> content;
   std::optional<transaction_inclusion_proof> transaction;

   bool operator==(const audit_bundle&) const = default;
};

struct response_context {
   chain_id chain;
   block_id head;
   block_id finalized;
   std::optional<state_anchor> anchor;

   bool operator==(const response_context&) const = default;
};

struct audited_response {
   response_context context;
   std::optional<audit_bundle> audit;

   bool operator==(const audited_response&) const = default;
};

struct method_capability {
   std::string api;
   std::string method;
   audit_class audit = audit_class::none;
   bool enabled = false;
   bool http = false;
   bool p2p = false;

   bool operator==(const method_capability&) const = default;
};

struct capabilities {
   std::vector<method_capability> methods;
   bool archive = false;

   bool operator==(const capabilities&) const = default;
};

struct service_limits {
   std::uint32_t max_page_size = 1'024;
   std::uint32_t max_state_batch_size = 128;
   std::uint32_t max_transaction_batch_size = 128;
   std::uint32_t max_container_elements = 4'096;
   std::uint32_t max_transaction_status_candidates = 4'096;
   std::uint32_t max_request_bytes = 16U << 20U;
   std::uint32_t max_response_bytes = 16U << 20U;
   std::uint32_t max_proof_bytes = 8U << 20U;
   std::uint64_t max_await_ms = 300'000;
   std::uint32_t state_retention_blocks = 4'096;

   bool operator==(const service_limits&) const = default;
};

BOOST_DESCRIBE_ENUM(audit_mode, none, if_available, required)
BOOST_DESCRIBE_ENUM(audit_class, none, finality, state_point, state_range, state_changes, transaction_inclusion,
                    deterministic_composite, unsupported)
BOOST_DESCRIBE_STRUCT(state_anchor, (),
                      (chain, block, block_num, transaction_root, state_root, state_size, change_root, change_count))
BOOST_DESCRIBE_STRUCT(anchored_request, (), (anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(proof_blob, (), (scheme, version, payload))
BOOST_DESCRIBE_STRUCT(content_witness, (), (hash, value))
BOOST_DESCRIBE_STRUCT(transaction_inclusion_proof, (), (leaf, index, leaf_count, path))
BOOST_DESCRIBE_STRUCT(audit_bundle, (), (finality, ancestry, state, content, transaction))
BOOST_DESCRIBE_STRUCT(response_context, (), (chain, head, finalized, anchor))
BOOST_DESCRIBE_STRUCT(audited_response, (), (context, audit))
BOOST_DESCRIBE_STRUCT(method_capability, (), (api, method, audit, enabled, http, p2p))
BOOST_DESCRIBE_STRUCT(capabilities, (), (methods, archive))
BOOST_DESCRIBE_STRUCT(service_limits, (),
                      (max_page_size, max_state_batch_size, max_transaction_batch_size, max_container_elements,
                       max_transaction_status_candidates, max_request_bytes, max_response_bytes, max_proof_bytes,
                       max_await_ms, state_retention_blocks))

} // namespace forge::chain::protocol
