module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module forge.chain.protocol.state_query;

import forge.chain.protocol.action;
import forge.chain.protocol.code_hash_result;
import forge.chain.protocol.transaction;
import forge.variant.containers;
import forge.variant.described;

export import forge.chain.protocol.audit;
export import forge.chain.protocol.authority;
export import forge.variant.value;

export namespace forge::chain::protocol {

struct key_range {
   std::optional<bytes> lower;
   std::optional<bytes> upper;

   bool operator==(const key_range&) const = default;
};

struct state_point_request {
   bytes key;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const state_point_request&) const = default;
};

struct state_range_request {
   key_range range;
   std::optional<block_id> anchor;
   std::uint32_t limit = 256;
   bool reverse = false;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const state_range_request&) const = default;
};

struct state_range_item {
   bytes key;
   bytes value;

   bool operator==(const state_range_item&) const = default;
};

struct state_point_response : audited_response {
   std::optional<bytes> value;

   bool operator==(const state_point_response&) const = default;
};

struct state_range_response : audited_response {
   std::vector<state_range_item> rows;
   std::optional<bytes> next_key;

   bool operator==(const state_range_response&) const = default;
};

struct state_mutation {
   bytes key;
   std::optional<bytes> value;

   bool operator==(const state_mutation&) const = default;
};

struct state_change_range {
   key_range range;
   std::vector<state_mutation> mutations;
   std::optional<bytes> next_key;

   bool operator==(const state_change_range&) const = default;
};

struct state_change_batch {
   state_anchor anchor;
   std::vector<state_change_range> ranges;

   bool operator==(const state_change_batch&) const = default;
};

struct state_changes_cursor {
   std::uint32_t block = 0;
   std::uint32_t range = 0;
   std::optional<bytes> key;

   bool operator==(const state_changes_cursor&) const = default;
};

struct state_changes_request {
   std::uint32_t from_block = 0;
   std::uint32_t to_block = 0;
   std::vector<key_range> ranges;
   std::uint32_t limit = 256;
   std::optional<state_changes_cursor> cursor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const state_changes_request&) const = default;
};

struct state_changes_response : audited_response {
   std::vector<state_change_batch> blocks;
   std::optional<state_changes_cursor> next;

   bool operator==(const state_changes_response&) const = default;
};

struct account_request {
   forge::chain::protocol::account_name account;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const account_request&) const = default;
};

struct account_permission {
   forge::chain::protocol::permission_name name;
   forge::chain::protocol::permission_name parent;
   forge::chain::protocol::authority auth;

   bool operator==(const account_permission&) const = default;
};

struct account_response : audited_response {
   forge::chain::protocol::account_name account;
   forge::chain::protocol::block_timestamp creation_date;
   std::vector<account_permission> permissions;

   bool operator==(const account_response&) const = default;
};

struct code_request {
   forge::chain::protocol::account_name account;
   bool include_code = true;
   bool include_abi = true;
   std::optional<digest> known_abi_hash;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const code_request&) const = default;
};

struct code_response : audited_response {
   forge::chain::protocol::account_name account;
   forge::chain::protocol::code_hash_result hash;
   digest abi_hash;
   std::optional<bytes> wasm;
   std::optional<bytes> raw_abi;

   bool operator==(const code_response&) const = default;
};

enum class table_index_kind : std::uint8_t {
   primary = 0,
   secondary_u64 = 1,
   secondary_u128 = 2,
   secondary_u256 = 3,
   secondary_f64 = 4,
   secondary_f128 = 5,
};

struct table_index {
   table_index_kind kind = table_index_kind::primary;
   std::uint8_t position = 0;

   [[nodiscard]] static table_index from_string(std::string_view value);
   [[nodiscard]] std::string to_string() const;

   bool operator==(const table_index&) const = default;
};

struct table_row {
   bytes value;
   std::optional<forge::chain::protocol::account_name> payer;

   bool operator==(const table_row&) const = default;
};

struct table_rows_request {
   forge::chain::protocol::account_name code;
   forge::chain::protocol::name scope;
   forge::chain::protocol::name table;
   table_index index;
   std::optional<bytes> lower_bound;
   std::optional<bytes> upper_bound;
   std::optional<bytes> cursor;
   std::uint32_t limit = 10;
   bool reverse = false;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const table_rows_request&) const = default;
};

struct table_rows_response : audited_response {
   std::vector<table_row> rows;
   std::optional<bytes> next;

   bool operator==(const table_rows_response&) const = default;
};

struct table_scope_request {
   forge::chain::protocol::account_name code;
   forge::chain::protocol::name table;
   std::string lower_bound;
   std::string upper_bound;
   std::uint32_t limit = 10;
   bool reverse = false;
   std::optional<bytes> cursor;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const table_scope_request&) const = default;
};

struct table_scope_row {
   forge::chain::protocol::name code;
   forge::chain::protocol::name scope;
   forge::chain::protocol::name table;
   forge::chain::protocol::account_name payer;
   std::uint32_t count = 0;

   bool operator==(const table_scope_row&) const = default;
};

struct table_scope_response : audited_response {
   std::vector<table_scope_row> rows;
   std::optional<bytes> next;

   bool operator==(const table_scope_response&) const = default;
};

struct currency_balance_request {
   forge::chain::protocol::account_name code;
   forge::chain::protocol::account_name account;
   std::optional<forge::chain::protocol::symbol_code> symbol;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const currency_balance_request&) const = default;
};

struct currency_stats_request {
   forge::chain::protocol::account_name code;
   forge::chain::protocol::symbol_code symbol{};
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const currency_stats_request&) const = default;
};

struct currency_balance_response : audited_response {
   std::vector<asset> balances;

   bool operator==(const currency_balance_response&) const = default;
};

struct currency_stats_response : audited_response {
   forge::variant stats;

   bool operator==(const currency_stats_response&) const = default;
};

struct scheduled_request {
   bool json = false;
   std::string lower_bound;
   std::uint32_t limit = 50;
   std::optional<std::uint32_t> time_limit_ms;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const scheduled_request&) const = default;
};

struct scheduled_transaction {
   transaction_id trx_id;
   account_name sender;
   uint128_t sender_id = 0;
   account_name payer;
   time_point delay_until{microseconds{}};
   time_point expiration{microseconds{}};
   time_point published{microseconds{}};
   forge::variant transaction;

   bool operator==(const scheduled_transaction&) const = default;
};

struct scheduled_response : audited_response {
   std::vector<scheduled_transaction> transactions;
   std::string more;

   bool operator==(const scheduled_response&) const = default;
};

enum class authorizer_source : std::uint8_t {
   account = 0,
   key = 1,
};

struct authorizers_cursor {
   authorizer_source source = authorizer_source::account;
   std::uint32_t input = 0;
   std::optional<bytes> lower;

   bool operator==(const authorizers_cursor&) const = default;
};

struct authorizers_request {
   std::vector<forge::chain::protocol::permission_level> accounts;
   std::vector<forge::chain::protocol::public_key> keys;
   std::uint32_t limit = 256;
   std::optional<authorizers_cursor> cursor;
   std::optional<block_id> anchor;
   std::optional<block_id> finality_from;
   audit_mode audit = audit_mode::none;

   bool operator==(const authorizers_request&) const = default;
};

struct authorizer_match {
   forge::chain::protocol::account_name account_name;
   forge::chain::protocol::permission_name permission_name;
   std::optional<forge::chain::protocol::permission_level> authorizing_account;
   std::optional<forge::chain::protocol::public_key> authorizing_key;
   forge::chain::protocol::weight weight = 0;
   std::uint32_t threshold = 0;

   bool operator==(const authorizer_match&) const = default;
};

struct authorizers_response : audited_response {
   std::vector<authorizer_match> accounts;
   std::optional<authorizers_cursor> next;

   bool operator==(const authorizers_response&) const = default;
};

BOOST_DESCRIBE_STRUCT(key_range, (), (lower, upper))
BOOST_DESCRIBE_STRUCT(state_point_request, (), (key, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(state_range_request, (), (range, anchor, limit, reverse, finality_from, audit))
BOOST_DESCRIBE_STRUCT(state_range_item, (), (key, value))
BOOST_DESCRIBE_STRUCT(state_point_response, (audited_response), (value))
BOOST_DESCRIBE_STRUCT(state_range_response, (audited_response), (rows, next_key))
BOOST_DESCRIBE_STRUCT(state_mutation, (), (key, value))
BOOST_DESCRIBE_STRUCT(state_change_range, (), (range, mutations, next_key))
BOOST_DESCRIBE_STRUCT(state_change_batch, (), (anchor, ranges))
BOOST_DESCRIBE_STRUCT(state_changes_cursor, (), (block, range, key))
BOOST_DESCRIBE_STRUCT(state_changes_request, (), (from_block, to_block, ranges, limit, cursor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(state_changes_response, (audited_response), (blocks, next))
BOOST_DESCRIBE_STRUCT(account_request, (), (account, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(account_permission, (), (name, parent, auth))
BOOST_DESCRIBE_STRUCT(account_response, (audited_response), (account, creation_date, permissions))
BOOST_DESCRIBE_STRUCT(code_request, (),
                      (account, include_code, include_abi, known_abi_hash, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(code_response, (audited_response), (account, hash, abi_hash, wasm, raw_abi))
BOOST_DESCRIBE_ENUM(table_index_kind, primary, secondary_u64, secondary_u128, secondary_u256, secondary_f64,
                    secondary_f128)
BOOST_DESCRIBE_STRUCT(table_index, (), (kind, position))
BOOST_DESCRIBE_STRUCT(table_row, (), (value, payer))
BOOST_DESCRIBE_STRUCT(table_rows_request, (),
                      (code, scope, table, index, lower_bound, upper_bound, cursor, limit, reverse, anchor,
                       finality_from, audit))
BOOST_DESCRIBE_STRUCT(table_rows_response, (audited_response), (rows, next))
BOOST_DESCRIBE_STRUCT(table_scope_request, (),
                      (code, table, lower_bound, upper_bound, limit, reverse, cursor, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(table_scope_row, (), (code, scope, table, payer, count))
BOOST_DESCRIBE_STRUCT(table_scope_response, (audited_response), (rows, next))
BOOST_DESCRIBE_STRUCT(currency_balance_request, (), (code, account, symbol, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(currency_stats_request, (), (code, symbol, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(currency_balance_response, (audited_response), (balances))
BOOST_DESCRIBE_STRUCT(currency_stats_response, (audited_response), (stats))
BOOST_DESCRIBE_STRUCT(scheduled_request, (), (json, lower_bound, limit, time_limit_ms, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(scheduled_transaction, (),
                      (trx_id, sender, sender_id, payer, delay_until, expiration, published, transaction))
BOOST_DESCRIBE_STRUCT(scheduled_response, (audited_response), (transactions, more))
BOOST_DESCRIBE_ENUM(authorizer_source, account, key)
BOOST_DESCRIBE_STRUCT(authorizers_cursor, (), (source, input, lower))
BOOST_DESCRIBE_STRUCT(authorizers_request, (), (accounts, keys, limit, cursor, anchor, finality_from, audit))
BOOST_DESCRIBE_STRUCT(authorizer_match, (),
                      (account_name, permission_name, authorizing_account, authorizing_key, weight, threshold))
BOOST_DESCRIBE_STRUCT(authorizers_response, (audited_response), (accounts, next))

} // namespace forge::chain::protocol
