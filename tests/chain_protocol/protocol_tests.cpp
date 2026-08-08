#include "spring_fixtures.hpp"

#include <boost/test/unit_test.hpp>

#include <array>
#include <concepts>
#include <deque>
#include <flat_map>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import forge.crypto.asymmetric;
import forge.crypto.asymmetric.secp256k1;
import forge.crypto.digest.sha256;
import forge.compression.exceptions;
import forge.codec.json;
import forge.raw.raw;
import forge.raw.exceptions;
import forge.variant.described;
import forge.variant.exceptions;
import forge.variant.value;
import forge.chain.core.merkle;
import forge.chain.protocol.abi;
import forge.chain.protocol.action;
import forge.chain.protocol.action_receipt;
import forge.chain.protocol.admin;
import forge.chain.protocol.block;
import forge.chain.protocol.blockchain_parameters;
import forge.chain.protocol.call_access_mode;
import forge.chain.protocol.call_data_header;
import forge.chain.protocol.code_hash_result;
import forge.chain.protocol.finalizer_policy;
import forge.chain.protocol.fixed_key;
import forge.chain.protocol.hash_id;
import forge.chain.protocol.kv_parameters;
import forge.chain.protocol.system;
import forge.chain.protocol.transaction;
import forge.chain.protocol.types;

namespace core = forge::chain::core;
namespace protocol = forge::chain::protocol;
namespace spring = forge::tests::spring_fixtures;

static_assert(std::same_as<protocol::digest, core::digest>);

namespace {

std::string expected(std::string_view value) {
   return std::string{value};
}

bool has_message(const std::exception& error, std::string_view message) {
   return error.what() == message;
}

struct named_action_payload {
   std::uint64_t workspace = 0;
   std::uint64_t inode = 0;

   static constexpr protocol::action_name get_name() {
      return protocol::make_name("beginrev");
   }
};

template <typename Stream> void raw_pack(Stream& stream, const named_action_payload& value) {
   forge::raw::pack(stream, value.workspace);
   forge::raw::pack(stream, value.inode);
}

template <typename Stream> void raw_unpack(Stream& stream, named_action_payload& value) {
   forge::raw::unpack(stream, value.workspace);
   forge::raw::unpack(stream, value.inode);
}

std::string hex(std::span<const std::uint8_t> bytes) {
   std::ostringstream out;
   out << std::hex << std::setfill('0');
   for (const auto byte : bytes) {
      out << std::setw(2) << static_cast<unsigned>(byte);
   }
   return out.str();
}

protocol::bytes unhex(std::string_view value) {
   auto out = protocol::bytes{};
   out.reserve(value.size() / 2);
   for (auto index = std::size_t{0}; index < value.size(); index += 2) {
      const auto byte = std::string{value.substr(index, 2)};
      out.push_back(static_cast<std::uint8_t>(std::stoi(byte, nullptr, 16)));
   }
   return out;
}

template <typename T> std::string pack_hex(const T& value) {
   return hex(forge::raw::pack(value));
}

std::string word_hex(protocol::uint128_t value) {
   std::ostringstream out;
   out << std::hex << std::setfill('0') << std::setw(16) << static_cast<std::uint64_t>(value >> 64U) << std::setw(16)
       << static_cast<std::uint64_t>(value);
   return out.str();
}

template <std::size_t Size> std::string backing_hex(const protocol::fixed_key<Size>& value) {
   auto result = std::string{};
   for (const auto word : value.get_array()) {
      result += word_hex(word);
   }
   return result;
}

template <typename Key, typename Word>
concept supports_single_word_factory = requires(Word word) {
   { Key::template make_from_word_sequence<Word>(word) } -> std::same_as<Key>;
};

template <typename Key, typename Word>
concept supports_word_pair_factory = requires(Word first, Word second) {
   { Key::template make_from_word_sequence<Word>(first, second) } -> std::same_as<Key>;
};

static_assert(std::constructible_from<protocol::key256, std::array<std::uint32_t, 5>>);
static_assert(!std::constructible_from<protocol::key256, std::array<protocol::uint128_t, 1>>);
static_assert(supports_single_word_factory<protocol::key256, protocol::uint128_t>);
static_assert(supports_word_pair_factory<protocol::key256, protocol::uint128_t>);
static_assert(!supports_single_word_factory<protocol::key256, std::int64_t>);
static_assert(protocol::fixed_key<1>::num_words() == 1U && protocol::fixed_key<1>::padded_bytes() == 15U);
static_assert(protocol::fixed_key<20>::num_words() == 2U && protocol::fixed_key<20>::padded_bytes() == 12U);
static_assert(protocol::fixed_key<32>::num_words() == 2U && protocol::fixed_key<32>::padded_bytes() == 0U);
static_assert(protocol::fixed_key<64>::num_words() == 4U && protocol::fixed_key<64>::padded_bytes() == 0U);

protocol::signature parse_spring_signature(std::string_view value) {
   return forge::crypto::asymmetric::encoding::antelope().parse_signature(value);
}

protocol::public_key parse_spring_public_key(std::string_view value) {
   return forge::crypto::asymmetric::encoding::antelope().parse_public(value);
}

std::string format_spring_public_key(const protocol::public_key& value) {
   return forge::crypto::asymmetric::encoding::antelope().format(value);
}

protocol::action make_setabi_action() {
   protocol::action value;
   value.account = protocol::make_name("eosio");
   value.name = protocol::make_name("setabi");
   value.data = {char{0x01}, char{0x02}};
   return value;
}

protocol::action_receipt make_reference_action_receipt() {
   const auto action = make_setabi_action();
   const auto return_value = protocol::bytes{0x80, 0xff, 0x00};

   auto receipt = protocol::action_receipt{};
   receipt.receiver = action.account;
   receipt.act_digest = protocol::generate_action_digest(action, return_value);
   receipt.global_sequence = 0x0102030405060708ULL;
   receipt.recv_sequence = 9U;
   receipt.auth_sequence.emplace(protocol::account_name{2U}, 12U);
   receipt.auth_sequence.emplace(protocol::account_name{1U}, 11U);
   receipt.code_sequence = 127U;
   receipt.abi_sequence = 128U;
   return receipt;
}

std::vector<protocol::bytes> make_context_free_data() {
   return {{char{0x03}, char{0x04}}};
}

protocol::transaction make_reference_transaction() {
   protocol::transaction value;
   value.expiration = std::chrono::sys_seconds{};
   value.ref_block_num = 1;
   value.ref_block_prefix = 0xaabbccdd;
   value.actions = {make_setabi_action()};
   return value;
}

protocol::signed_transaction make_reference_signed_transaction() {
   auto value = protocol::signed_transaction{};
   static_cast<protocol::transaction&>(value) = make_reference_transaction();
   value.signatures = {parse_spring_signature(spring::transaction_signature)};
   value.context_free_data = make_context_free_data();
   return value;
}

protocol::abi_def make_reference_abi() {
   return protocol::abi_def{
       .version = "eosio::abi/1.2",
       .types = {protocol::type_def{.new_type_name = "account_name", .type = "name"}},
       .actions = {protocol::action_def{
           .name = protocol::make_name("setabi"),
           .type = "setabi",
       }},
   };
}

std::string legacy_abi_hex() {
   constexpr auto empty_optional_vector_hex_size = std::size_t{2};
   return std::string{spring::abi_raw.substr(0, spring::abi_raw.size() - 2U * empty_optional_vector_hex_size)};
}

protocol::block_header make_reference_block_header() {
   protocol::block_header header;
   header.timestamp = protocol::block_timestamp{1};
   header.producer = protocol::make_name("eosio");
   header.confirmed = 0;
   header.previous = {};
   header.transaction_mroot = protocol::calculate_transaction_id(make_reference_transaction());
   header.action_mroot = {};
   return header;
}

protocol::transaction_receipt make_reference_receipt() {
   protocol::transaction_receipt receipt;
   receipt.status = protocol::transaction_receipt::status::executed;
   receipt.trx = protocol::calculate_transaction_id(make_reference_transaction());
   return receipt;
}

protocol::transaction_receipt make_receipt(std::uint32_t cpu_usage_us) {
   auto value = protocol::transaction_receipt{};
   value.status = protocol::transaction_receipt::status::executed;
   value.cpu_usage_us = cpu_usage_us;
   value.trx = protocol::transaction_id::hash("transaction-" + std::to_string(cpu_usage_us));
   return value;
}

protocol::signed_block_header make_reference_signed_block_header() {
   auto header = protocol::signed_block_header{};
   static_cast<protocol::block_header&>(header) = make_reference_block_header();
   header.producer_signature = parse_spring_signature(spring::block_signature);
   return header;
}

protocol::signed_block make_reference_signed_block() {
   auto block = protocol::signed_block{};
   static_cast<protocol::signed_block_header&>(block) = make_reference_signed_block_header();
   block.transactions.emplace_back(make_reference_receipt());
   block.block_extensions = {{7, {char{0x0a}, char{0x0b}}}};
   return block;
}

} // namespace

BOOST_AUTO_TEST_SUITE(forge_chain_protocol_compatibility)

static_assert(std::is_same_v<protocol::public_key, forge::crypto::asymmetric::public_key>);
static_assert(std::is_same_v<protocol::signature, forge::crypto::asymmetric::signature>);

BOOST_AUTO_TEST_CASE(fixed_key_matches_donor_word_and_byte_order) {
   const auto high = static_cast<protocol::uint128_t>(0x0102030405060708ULL) << 64U |
                     static_cast<protocol::uint128_t>(0x1112131415161718ULL);
   const auto low = static_cast<protocol::uint128_t>(0x2122232425262728ULL) << 64U |
                    static_cast<protocol::uint128_t>(0x3132333435363738ULL);
   const auto value = protocol::key256{std::array<protocol::uint128_t, 2>{high, low}};

   BOOST_TEST(hex(value.extract_as_byte_array()) == "0102030405060708111213141516171821222324252627283132333435363738");
   BOOST_TEST(pack_hex(value) == "0102030405060708111213141516171821222324252627283132333435363738");
   BOOST_TEST((value == forge::raw::unpack<protocol::key256>(forge::raw::pack(value))));

   const auto variant = forge::variant{value};
   BOOST_TEST(variant.get_string() == "0102030405060708111213141516171821222324252627283132333435363738");
   BOOST_TEST((variant.as<protocol::key256>() == value));
}

BOOST_AUTO_TEST_CASE(contract_wire_records_preserve_spring_raw_layout) {
   auto code_hash = protocol::code_hash_result{};
   code_hash.struct_version = forge::unsigned_int{1U};
   code_hash.code_sequence = 0x0102030405060708ULL;
   code_hash.code_hash = protocol::checksum256{std::array<std::uint8_t, 32>{
       0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU,
       0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U, 0x18U, 0x19U, 0x1aU, 0x1bU, 0x1cU, 0x1dU, 0x1eU, 0x1fU,
   }};
   code_hash.vm_type = 0xaaU;
   code_hash.vm_version = 0xbbU;
   BOOST_TEST(pack_hex(code_hash) ==
              "010807060504030201000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1faabb");

   const auto parameters = protocol::blockchain_parameters{
       .max_block_net_usage = 1U,
       .target_block_net_usage_pct = 2U,
       .max_transaction_net_usage = 3U,
       .base_per_transaction_net_usage = 4U,
       .net_usage_leeway = 5U,
       .context_free_discount_net_usage_num = 6U,
       .context_free_discount_net_usage_den = 7U,
       .max_block_cpu_usage = 8U,
       .target_block_cpu_usage_pct = 9U,
       .max_transaction_cpu_usage = 10U,
       .min_transaction_cpu_usage = 11U,
       .max_transaction_lifetime = 12U,
       .deferred_trx_expiration_window = 13U,
       .max_transaction_delay = 14U,
       .max_inline_action_size = 15U,
       .max_inline_action_depth = 16U,
       .max_authority_depth = 17U,
   };
   BOOST_TEST(pack_hex(parameters) ==
              "010000000000000002000000030000000400000005000000060000000700000008000000090000000a0000000b000000"
              "0c0000000d0000000e0000000f00000010001100");

   const auto kv = protocol::kv_parameters{.max_key_size = 1U, .max_value_size = 2U, .max_iterators = 3U};
   BOOST_TEST(pack_hex(kv) == "010000000200000003000000");

   const auto authority = protocol::finalizer_authority{
       .description = "f",
       .weight = 3U,
       .public_key = {char{0x01}, char{0x02}},
   };
   BOOST_TEST(pack_hex(authority) == "01660300000000000000020102");
   const auto policy = protocol::finalizer_policy{.threshold = 4U, .finalizers = {authority}};
   BOOST_TEST(pack_hex(policy) == "04000000000000000101660300000000000000020102");

   const auto call = protocol::call_data_header{.version = 0x01020304U, .func_name = 0x0102030405060708ULL};
   BOOST_TEST(pack_hex(call) == "040302010807060504030201");
   BOOST_TEST(static_cast<std::uint8_t>(protocol::call_access_mode::read_write) == 0U);
   BOOST_TEST(static_cast<std::uint8_t>(protocol::call_access_mode::read_only) == 1U);
   const auto apply_id = static_cast<protocol::hash_id::raw>(protocol::hash_id{"apply"});
   BOOST_TEST(static_cast<std::uint64_t>(apply_id) == protocol::hash_id::hash(std::string{"apply"}));
}

BOOST_AUTO_TEST_CASE(fixed_key_partial_word_sequences_preserve_cdt_layout) {
   const auto u8 = protocol::key256::make_from_word_sequence<std::uint8_t>(std::uint8_t{1U});
   const auto u16 = protocol::key256::make_from_word_sequence<std::uint16_t>(std::uint16_t{1U});
   const auto u32 = protocol::key256::make_from_word_sequence<std::uint32_t>(std::uint32_t{1U});
   const auto u32_pair = protocol::key256::make_from_word_sequence<std::uint32_t>(std::uint32_t{1U}, std::uint32_t{2U});
   const auto u32_triple = protocol::key256::make_from_word_sequence<std::uint32_t>(
       std::uint32_t{1U}, std::uint32_t{2U}, std::uint32_t{3U});
   const auto u32_full = protocol::key256::make_from_word_sequence<std::uint32_t>(std::uint32_t{1U}, std::uint32_t{2U},
                                                                                  std::uint32_t{3U}, std::uint32_t{4U});
   const auto u32_crossing = protocol::key256::make_from_word_sequence<std::uint32_t>(
       std::uint32_t{1U}, std::uint32_t{2U}, std::uint32_t{3U}, std::uint32_t{4U}, std::uint32_t{5U});
   const auto u64 = protocol::key256::make_from_word_sequence<std::uint64_t>(std::uint64_t{1U});
   const auto u64_full = protocol::key256::make_from_word_sequence<std::uint64_t>(std::uint64_t{1U}, std::uint64_t{2U},
                                                                                  std::uint64_t{3U}, std::uint64_t{4U});
   const auto u128 = protocol::key256::make_from_word_sequence<protocol::uint128_t>(protocol::uint128_t{1U});
   const auto u128_full =
       protocol::key256::make_from_word_sequence<protocol::uint128_t>(protocol::uint128_t{1U}, protocol::uint128_t{2U});

   BOOST_TEST(backing_hex(u8) == "0100000000000000000000000000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u16) == "0000000000000001000000000000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u32) == "0000000000000000000100000000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u32_pair) == "0000000000000100000002000000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u32_triple) == "0000000100000002000000030000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u32_full) == "0000000100000002000000030000000400000000000000000000000000000000");
   BOOST_TEST(backing_hex(u32_crossing) == "0000000100000002000000030000000400000000000000000005000000000000");
   BOOST_TEST(backing_hex(u64) == "0000000000000001000000000000000000000000000000000000000000000000");
   BOOST_TEST(backing_hex(u64_full) == "0000000000000001000000000000000200000000000000030000000000000004");
   BOOST_TEST(backing_hex(u128) == "0000000000000000000000000000000100000000000000000000000000000000");
   BOOST_TEST(backing_hex(u128_full) == "0000000000000000000000000000000100000000000000000000000000000002");

   BOOST_TEST((u32_crossing == protocol::key256{std::array<std::uint32_t, 5>{1U, 2U, 3U, 4U, 5U}}));
   BOOST_TEST(pack_hex(u32) == "0000000000000000000100000000000000000000000000000000000000000000");
   BOOST_TEST(hex(u64_full.extract_as_byte_array()) ==
              "0000000000000001000000000000000200000000000000030000000000000004");
}

BOOST_AUTO_TEST_CASE(fixed_key_orders_lexicographically_and_rejects_invalid_text) {
   const auto lower = protocol::key256::make_from_word_sequence<std::uint64_t>(std::uint64_t{1U}, std::uint64_t{2U},
                                                                               std::uint64_t{3U}, std::uint64_t{4U});
   const auto higher = protocol::key256::make_from_word_sequence<std::uint64_t>(std::uint64_t{1U}, std::uint64_t{2U},
                                                                                std::uint64_t{3U}, std::uint64_t{5U});
   BOOST_TEST(lower < higher);

   BOOST_CHECK_THROW(forge::variant{"00"}.as<protocol::key256>(), forge::variant_exceptions::decode_error);
   BOOST_CHECK_THROW(forge::variant{std::string(63U, '0') + "x"}.as<protocol::key256>(),
                     forge::variant_exceptions::decode_error);
}

BOOST_AUTO_TEST_CASE(fixed_key_supports_exact_bytes_padding_and_truncated_raw_rejection) {
   using key160 = protocol::fixed_key<20>;
   static_assert(key160::num_words() == 2U);
   static_assert(key160::padded_bytes() == 12U);

   const auto bytes = std::array<std::uint8_t, 20>{
       0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
       0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
   };
   const auto value = key160{bytes};
   BOOST_TEST(value.extract_as_byte_array() == bytes);
   BOOST_TEST(pack_hex(value) == "000102030405060708090a0b0c0d0e0f10111213");

   auto truncated = forge::raw::pack(value);
   truncated.pop_back();
   BOOST_CHECK_THROW((void)forge::raw::unpack<key160>(truncated), forge::raw::exceptions::codec_error);
}

BOOST_AUTO_TEST_CASE(fixed_key_padded_storage_and_order_match_cdt_fixed_bytes) {
   using key160 = protocol::fixed_key<20>;
   const auto first = static_cast<protocol::uint128_t>(0x0102030405060708ULL) << 64U |
                      static_cast<protocol::uint128_t>(0x1112131415161718ULL);
   const auto second = static_cast<protocol::uint128_t>(0x2122232425262728ULL) << 64U |
                       static_cast<protocol::uint128_t>(0x3132333435363738ULL);
   const auto value = key160{std::array<protocol::uint128_t, 2>{first, second}};

   BOOST_TEST(backing_hex(value) == "0102030405060708111213141516171821222324252627283132333435363738");
   BOOST_TEST(hex(value.extract_as_byte_array()) == "0102030405060708111213141516171821222324");

   const auto bytes = std::array<std::uint8_t, 20>{
       0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
       0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
   };
   const auto canonical = key160{bytes};
   BOOST_TEST(backing_hex(canonical) == "000102030405060708090a0b0c0d0e0f10111213000000000000000000000000");
   BOOST_TEST((canonical == forge::raw::unpack<key160>(forge::raw::pack(canonical))));

   const auto visible = static_cast<protocol::uint128_t>(0xaabbccddULL) << 96U;
   const auto lower = key160{std::array<protocol::uint128_t, 2>{0U, visible | 1U}};
   const auto higher = key160{std::array<protocol::uint128_t, 2>{0U, visible | 2U}};
   BOOST_TEST(lower.extract_as_byte_array() == higher.extract_as_byte_array());
   BOOST_TEST(lower < higher);
}

BOOST_AUTO_TEST_CASE(fixed_key_zero_size_matches_donor_value_semantics) {
   using empty_key = protocol::fixed_key<0>;
   static_assert(empty_key::num_words() == 0U);
   static_assert(empty_key::padded_bytes() == 0U);

   const auto value = empty_key{};
   BOOST_TEST(value.get_array().empty());
   BOOST_TEST(value.extract_as_byte_array().empty());
   BOOST_TEST(forge::raw::pack(value).empty());
   BOOST_TEST((forge::raw::unpack<empty_key>(forge::raw::bytes{}) == value));

   const auto variant = forge::variant{value};
   BOOST_TEST(variant.get_string().empty());
   BOOST_TEST((variant.as<empty_key>() == value));
}

BOOST_AUTO_TEST_CASE(name_symbol_and_asset_match_spring_fixtures) {
   const auto eosio = protocol::make_name("eosio");
   const auto active = protocol::make_name("active");

   BOOST_TEST(eosio.value == spring::name_eosio_value);
   BOOST_TEST(active.value == spring::name_active_value);
   BOOST_TEST(protocol::to_string(eosio) == "eosio");
   BOOST_TEST(pack_hex(eosio) == expected(spring::name_eosio_raw));

   const auto token = protocol::asset{42, protocol::make_symbol("SYS", 4)};
   BOOST_TEST(pack_hex(token) == expected(spring::asset_raw));

   constexpr auto source_compatible = protocol::asset{42};
   static_assert(source_compatible.amount == 42);
   static_assert(source_compatible.sym.raw() == 0U);
}

BOOST_AUTO_TEST_CASE(name_rejects_high_valued_thirteenth_character) {
   BOOST_CHECK_NO_THROW((void)protocol::make_name("abcdefghijklj"));
   BOOST_CHECK_THROW(protocol::make_name("abcdefghijklk"), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::make_name("abcdefghijklz"), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(name_char_to_value_preserves_cdt_invalid_character_failure) {
   BOOST_TEST(protocol::name::char_to_value('.') == 0U);
   BOOST_TEST(protocol::name::char_to_value('1') == 1U);
   BOOST_TEST(protocol::name::char_to_value('a') == 6U);
   BOOST_CHECK_THROW((void)protocol::name::char_to_value('A'), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(asset_variant_text_preserves_precision) {
   auto variant = forge::variant{};

   protocol::to_variant(protocol::asset{42, protocol::make_symbol("SYS", 4)}, variant);
   BOOST_TEST(variant.as_string() == "0.0042 SYS");

   protocol::to_variant(protocol::asset{42, protocol::make_symbol("SYS", 0)}, variant);
   BOOST_TEST(variant.as_string() == "42 SYS");

   protocol::to_variant(protocol::asset{-42, protocol::make_symbol("SYS", 4)}, variant);
   BOOST_TEST(variant.as_string() == "-0.0042 SYS");
}

BOOST_AUTO_TEST_CASE(symbol_and_asset_variant_parse_canonical_text) {
   auto symbol = protocol::symbol{};
   protocol::from_variant(forge::variant{"4,SYS"}, symbol);
   BOOST_TEST(symbol.raw() == protocol::make_symbol("SYS", 4).raw());

   auto asset = protocol::asset{};
   protocol::from_variant(forge::variant{"0.0042 SYS"}, asset);
   const auto fractional_asset = protocol::asset{42, protocol::make_symbol("SYS", 4)};
   BOOST_TEST(asset.amount == fractional_asset.amount);
   BOOST_TEST(asset.sym.raw() == fractional_asset.sym.raw());

   protocol::from_variant(forge::variant{"42 SYS"}, asset);
   const auto whole_asset = protocol::asset{42, protocol::make_symbol("SYS", 0)};
   BOOST_TEST(asset.amount == whole_asset.amount);
   BOOST_TEST(asset.sym.raw() == whole_asset.sym.raw());

   protocol::from_variant(forge::variant{"-0.0042 SYS"}, asset);
   const auto negative_asset = protocol::asset{-42, protocol::make_symbol("SYS", 4)};
   BOOST_TEST(asset.amount == negative_asset.amount);
   BOOST_TEST(asset.sym.raw() == negative_asset.sym.raw());
}

BOOST_AUTO_TEST_CASE(asset_variant_parse_rejects_invalid_text) {
   auto asset = protocol::asset{};

   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"0.0042"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"0.0042 sys"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"0. SYS"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{".0042 SYS"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"0.0042 SYS extra"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"+1 SYS"}, asset), std::invalid_argument);
   BOOST_CHECK_THROW(protocol::from_variant(forge::variant{"9223372036854775808 SYS"}, asset), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(asset_arithmetic_preserves_cdt_checks_and_errors) {
   const auto sys = protocol::make_symbol("SYS", 4);
   const auto eos = protocol::make_symbol("EOS", 4);

   auto value = protocol::asset{42, sys};
   value += protocol::asset{8, sys};
   BOOST_TEST(value.amount == 50);
   value -= protocol::asset{20, sys};
   BOOST_TEST(value.amount == 30);
   value *= 3;
   BOOST_TEST(value.amount == 90);
   value /= 2;
   BOOST_TEST(value.amount == 45);

   BOOST_CHECK_EXCEPTION((void)(protocol::asset{protocol::asset::max_amount, sys} + protocol::asset{1, sys}),
                         std::invalid_argument,
                         [](const auto& error) { return has_message(error, "addition overflow"); });
   BOOST_CHECK_EXCEPTION((void)(protocol::asset{-protocol::asset::max_amount, sys} - protocol::asset{1, sys}),
                         std::invalid_argument,
                         [](const auto& error) { return has_message(error, "subtraction underflow"); });
   BOOST_CHECK_EXCEPTION(
       (void)(protocol::asset{1, sys} + protocol::asset{1, eos}), std::invalid_argument,
       [](const auto& error) { return has_message(error, "attempt to add asset with different symbol"); });
   BOOST_CHECK_EXCEPTION((void)(protocol::asset{1, sys} / 0), std::invalid_argument,
                         [](const auto& error) { return has_message(error, "divide by zero"); });
   BOOST_CHECK_EXCEPTION((void)(protocol::asset{1, sys} == protocol::asset{1, eos}), std::invalid_argument,
                         [](const auto& error) {
                            return has_message(error, "comparison of assets with different symbols is not allowed");
                         });

   const auto first = protocol::extended_asset{protocol::asset{42, sys}, protocol::make_name("eosio.token")};
   const auto second = protocol::extended_asset{protocol::asset{1, sys}, protocol::make_name("other.token")};
   BOOST_CHECK_EXCEPTION(first + second, std::invalid_argument,
                         [](const auto& error) { return has_message(error, "type mismatch"); });
}

BOOST_AUTO_TEST_CASE(time_and_extended_asset_match_cdt_wire_layout) {
   const auto point = protocol::time_point{protocol::microseconds{0x0102030405060708LL}};
   const auto point_sec = protocol::time_point_sec{0x01020304U};
   const auto timestamp = protocol::block_timestamp{0x01020304U};
   const auto extended =
       protocol::extended_asset{protocol::asset{42, protocol::make_symbol("SYS", 4)}, protocol::make_name("eosio")};

   BOOST_TEST(pack_hex(point) == "0807060504030201");
   BOOST_TEST(pack_hex(point_sec) == "04030201");
   BOOST_TEST(pack_hex(timestamp) == "04030201");
   BOOST_TEST(pack_hex(extended) == "2a0000000000000004535953000000000000000000ea3055");

   const auto parsed = protocol::time_point::from_iso_string("2000-01-01T00:00:00");
   BOOST_TEST(parsed.time_since_epoch().count() == 946'684'800'000'000LL);
   BOOST_TEST(parsed.to_string() == "2000-01-01T00:00:00");
   BOOST_TEST(protocol::time_point_sec::from_iso_string("2000-01-01T00:00:00").to_string() == "2000-01-01T00:00:00");
   BOOST_TEST(protocol::block_timestamp{parsed}.slot == 0U);
   const auto half_second = protocol::block_timestamp{1U};
   BOOST_TEST(half_second.to_string() == "2000-01-01T00:00:00.500");
   BOOST_TEST(protocol::block_timestamp::from_iso_string(half_second.to_string()).slot == half_second.slot);
   auto half_second_variant = forge::variant{};
   protocol::to_variant(half_second, half_second_variant);
   auto half_second_roundtrip = protocol::block_timestamp{};
   protocol::from_variant(half_second_variant, half_second_roundtrip);
   BOOST_TEST(half_second_roundtrip.slot == half_second.slot);
   BOOST_CHECK_EXCEPTION((void)protocol::block_timestamp::from_iso_string("2000-01-01T00:00:00.250"),
                         std::invalid_argument,
                         [](const auto& error) { return has_message(error, "date parsing failed"); });
   BOOST_TEST(protocol::block_timestamp::maximum().slot == 0xffffU);
   BOOST_TEST(protocol::block_timestamp::maximum().next().slot == 0x10000U);
   BOOST_TEST(protocol::block_timestamp{parsed}.next().slot == 1U);
   BOOST_CHECK_EXCEPTION((void)protocol::block_timestamp{std::numeric_limits<std::uint32_t>::max()}.next(),
                         std::invalid_argument,
                         [](const auto& error) { return has_message(error, "block timestamp overflow"); });
}

BOOST_AUTO_TEST_CASE(time_point_host_json_preserves_fractional_microseconds) {
   const auto whole_second = protocol::time_point{protocol::microseconds{946'684'800'000'000LL}};
   const auto whole_second_encoded = forge::codec::json::write(whole_second);
   BOOST_REQUIRE(whole_second_encoded.ok());
   BOOST_TEST(whole_second_encoded.text == R"("2000-01-01T00:00:00")");

   const auto point = protocol::time_point{protocol::microseconds{946'684'800'123'456LL}};
   const auto encoded = forge::codec::json::write(point);
   BOOST_REQUIRE(encoded.ok());
   BOOST_TEST(encoded.text == "\"2000-01-01T00:00:00.123456\"");

   const auto decoded = forge::codec::json::read<protocol::time_point>(encoded.text);
   BOOST_REQUIRE(decoded.ok());
   BOOST_TEST(decoded.value.time_since_epoch().count() == point.time_since_epoch().count());

   const auto milliseconds = forge::codec::json::read<protocol::time_point>("\"2000-01-01T00:00:00.5\"");
   BOOST_REQUIRE(milliseconds.ok());
   BOOST_TEST(milliseconds.value.time_since_epoch().count() == 946'684'800'500'000LL);
   BOOST_TEST(milliseconds.value.to_string() == "2000-01-01T00:00:00.500");

   const auto single_microsecond = forge::codec::json::read<protocol::time_point>("\"2000-01-01T00:00:00.000001\"");
   BOOST_REQUIRE(single_microsecond.ok());
   BOOST_TEST(single_microsecond.value.time_since_epoch().count() == 946'684'800'000'001LL);

   for (const auto* malformed :
        {"\"2000-01-01T00:00:00.\"", "\"2000-01-01T00:00:00.1234567\"", "\"2000-01-01T00:00:00.12x\""}) {
      BOOST_TEST(!forge::codec::json::read<protocol::time_point>(malformed).ok());
   }
}

BOOST_AUTO_TEST_CASE(action_transaction_and_signed_transaction_match_spring_fixtures) {
   const auto action = make_setabi_action();
   BOOST_TEST(pack_hex(action) == expected(spring::action_raw));

   const auto trx = make_reference_transaction();
   BOOST_TEST(protocol::pack_transaction(trx).size() == 36U);
   BOOST_TEST(pack_hex(trx) == expected(spring::transaction_raw));
   BOOST_TEST(protocol::calculate_transaction_id(trx).str() == expected(spring::transaction_id));

   const auto signed_trx = make_reference_signed_transaction();
   BOOST_TEST(pack_hex(signed_trx) == expected(spring::signed_transaction_raw));

   const auto packed = protocol::packed_transaction{signed_trx};
   BOOST_TEST(pack_hex(packed) == expected(spring::packed_transaction_raw));
   BOOST_TEST(packed.id().str() == expected(spring::transaction_id));
   BOOST_TEST(packed.packed_digest().str() == expected(spring::packed_transaction_digest));
}

BOOST_AUTO_TEST_CASE(action_digest_with_return_value_matches_spring_savanna_fixture) {
   const auto action = make_setabi_action();
   const auto empty_return_value = protocol::bytes{};
   const auto return_value = protocol::bytes{0x80, 0xff, 0x00};

   BOOST_TEST(protocol::generate_action_digest(action, empty_return_value).str() ==
              expected(spring::action_digest_empty_return_value));
   BOOST_TEST(protocol::generate_action_digest(action, return_value).str() ==
              expected(spring::action_digest_with_return_value));
}

BOOST_AUTO_TEST_CASE(action_receipt_wire_and_savanna_digests_match_spring_fixtures) {
   const auto action = make_setabi_action();
   const auto receipt = make_reference_action_receipt();

   BOOST_TEST(pack_hex(receipt) == expected(spring::action_receipt_raw));
   BOOST_TEST(protocol::calculate_savanna_witness_hash(receipt).str() == expected(spring::savanna_witness_hash));
   BOOST_TEST(protocol::calculate_savanna_action_digest(receipt, action).str() ==
              expected(spring::savanna_action_digest));

   const auto unpacked = forge::raw::unpack<protocol::action_receipt>(forge::raw::pack(receipt));
   BOOST_TEST(pack_hex(unpacked) == expected(spring::action_receipt_raw));

   auto encoded = forge::variant{};
   forge::to_variant(receipt, encoded);
   auto decoded = protocol::action_receipt{};
   forge::from_variant(encoded, decoded);
   BOOST_TEST(pack_hex(decoded) == expected(spring::action_receipt_raw));
}

BOOST_AUTO_TEST_CASE(action_receipt_auth_sequence_is_canonical_and_order_independent) {
   const auto action = make_setabi_action();
   const auto canonical = make_reference_action_receipt();
   auto reordered = canonical;
   reordered.auth_sequence.clear();
   reordered.auth_sequence.emplace(protocol::account_name{1U}, 11U);
   reordered.auth_sequence.emplace(protocol::account_name{2U}, 12U);

   BOOST_TEST(forge::raw::pack(reordered) == forge::raw::pack(canonical));
   BOOST_TEST(protocol::calculate_savanna_witness_hash(reordered) ==
              protocol::calculate_savanna_witness_hash(canonical));
   BOOST_TEST(protocol::calculate_savanna_action_digest(reordered, action) ==
              protocol::calculate_savanna_action_digest(canonical, action));
}

BOOST_AUTO_TEST_CASE(savanna_action_receipt_digests_use_core_merkle) {
   const auto action = make_setabi_action();
   auto first = make_reference_action_receipt();
   auto second = first;
   second.recv_sequence = 10U;

   const auto digests = std::array{
       protocol::calculate_savanna_action_digest(first, action),
       protocol::calculate_savanna_action_digest(second, action),
   };

   BOOST_TEST(digests[0].str() == expected(spring::savanna_action_digest));
   BOOST_TEST(digests[1].str() == expected(spring::second_savanna_action_digest));
   BOOST_TEST(core::calculate_merkle_root(digests).str() == expected(spring::savanna_action_root));
}

BOOST_AUTO_TEST_CASE(zlib_packed_transaction_matches_spring_and_unpacks_from_wire) {
   const auto signed_trx = make_reference_signed_transaction();
   const auto packed = protocol::packed_transaction{signed_trx, protocol::packed_transaction::compression::zlib};

   BOOST_TEST(pack_hex(packed) == expected(spring::packed_transaction_zlib_raw));
   BOOST_TEST(packed.id().str() == expected(spring::transaction_id));
   BOOST_TEST(packed.packed_digest().str() == expected(spring::packed_transaction_zlib_digest));
   BOOST_TEST(pack_hex(packed.get_signed_transaction()) == expected(spring::signed_transaction_raw));

   const auto unpacked = forge::raw::unpack<protocol::packed_transaction>(unhex(spring::packed_transaction_zlib_raw));
   BOOST_TEST(unpacked.id().str() == expected(spring::transaction_id));
   BOOST_TEST(unpacked.packed_digest().str() == expected(spring::packed_transaction_zlib_digest));
   BOOST_TEST(pack_hex(unpacked.get_signed_transaction()) == expected(spring::signed_transaction_raw));
}

BOOST_AUTO_TEST_CASE(packed_transaction_unknown_compression_is_typed_failure) {
   auto packed = protocol::packed_transaction{make_reference_signed_transaction()};
   packed.compression = static_cast<decltype(packed.compression)>(0xff);

   BOOST_CHECK_THROW((void)packed.get_signed_transaction(), forge::compression::exceptions::invalid_input);
}

BOOST_AUTO_TEST_CASE(transaction_signature_preimage_digest_and_spring_signature_are_compatible) {
   const auto trx = make_reference_transaction();
   const auto chain_id = protocol::chain_id{std::string{spring::chain_id}};
   const auto cfd = make_context_free_data();

   BOOST_TEST(hex(protocol::signature_preimage(chain_id, trx, cfd)) ==
              expected(spring::transaction_signature_preimage));
   BOOST_TEST(protocol::signature_digest(chain_id, trx, cfd).str() == expected(spring::transaction_signature_digest));

   const auto signature = parse_spring_signature(spring::transaction_signature);
   const auto recovered =
       forge::crypto::asymmetric::recover(signature, core::digest{std::string{spring::transaction_signature_digest}});
   BOOST_TEST(format_spring_public_key(recovered) == expected(spring::test_public_key));
}

BOOST_AUTO_TEST_CASE(abi_and_system_actions_match_spring_fixtures) {
   BOOST_TEST(pack_hex(make_reference_abi()) == expected(spring::abi_raw));

   const auto setabi = protocol::setabi{
       .account = protocol::make_name("eosio"),
       .abi = {char{0x0a}, char{0x0b}},
   };
   BOOST_TEST(pack_hex(setabi) == expected(spring::setabi_raw));
}

BOOST_AUTO_TEST_CASE(legacy_abi_unpack_normalizes_tail_fields_on_pack) {
   const auto legacy_hex = legacy_abi_hex();
   const auto legacy_bytes = unhex(legacy_hex);

   const auto unpacked = forge::raw::unpack<protocol::abi_def>(legacy_bytes);

   BOOST_TEST(unpacked.variants.value.empty());
   BOOST_TEST(unpacked.action_results.value.empty());
   BOOST_TEST(pack_hex(unpacked) == expected(spring::abi_raw));
}

BOOST_AUTO_TEST_CASE(may_not_exist_variant_conversion_uses_value) {
   auto value = protocol::may_not_exist<std::vector<protocol::variant_def>>{};

   auto encoded = forge::variant{};
   protocol::to_variant(value, encoded);

   auto decoded = protocol::may_not_exist<std::vector<protocol::variant_def>>{};
   protocol::from_variant(encoded, decoded);

   BOOST_TEST(encoded.get_array().empty());
   BOOST_TEST(decoded.value.empty());
}

BOOST_AUTO_TEST_CASE(abi_variant_schema_uses_spring_field_names) {
   auto abi = protocol::abi_def{};
   abi.version = "eosio::abi/1.2";
   abi.tables = {protocol::table_def{
       .name = protocol::make_name("accounts"),
       .index_type = "i64",
       .key_names = {"owner"},
       .key_types = {"name"},
       .type = "account",
   }};
   abi.action_results.value = {protocol::action_result_def{
       .name = protocol::make_name("get"),
       .result_type = "account",
   }};

   auto encoded = forge::variant{};
   protocol::to_variant(abi, encoded);

   const auto& object = encoded.get_object();
   const auto& table = object["tables"].get_array().front().get_object();
   BOOST_TEST(table.contains("index_type"));
   BOOST_TEST(!table.contains("index"));
   BOOST_TEST(table["index_type"].as_string() == "i64");

   const auto& action_result = object["action_results"].get_array().front().get_object();
   BOOST_TEST(action_result.contains("result_type"));
   BOOST_TEST(!action_result.contains("result"));
   BOOST_TEST(action_result["result_type"].as_string() == "account");
}

BOOST_AUTO_TEST_CASE(abi_variant_roundtrip_preserves_wire_compatibility) {
   const auto reference = make_reference_abi();

   auto encoded = forge::variant{};
   protocol::to_variant(reference, encoded);

   auto decoded = protocol::abi_def{};
   protocol::from_variant(encoded, decoded);

   BOOST_TEST(pack_hex(decoded) == expected(spring::abi_raw));
}

BOOST_AUTO_TEST_CASE(legacy_abi_variant_conversion_uses_empty_tail_fields) {
   const auto legacy_hex = legacy_abi_hex();
   const auto unpacked = forge::raw::unpack<protocol::abi_def>(unhex(legacy_hex));

   auto encoded = forge::variant{};
   protocol::to_variant(unpacked, encoded);

   const auto& object = encoded.get_object();
   BOOST_TEST(object.contains("variants"));
   BOOST_TEST(object.contains("action_results"));
   BOOST_TEST(object["variants"].get_array().empty());
   BOOST_TEST(object["action_results"].get_array().empty());

   auto decoded = protocol::abi_def{};
   protocol::from_variant(encoded, decoded);

   BOOST_TEST(decoded.variants.value.empty());
   BOOST_TEST(decoded.action_results.value.empty());
   BOOST_TEST(pack_hex(decoded) == expected(spring::abi_raw));
}

BOOST_AUTO_TEST_CASE(block_header_receipt_and_signed_block_match_spring_fixtures) {
   const auto header = make_reference_block_header();

   BOOST_TEST(pack_hex(header) == expected(spring::block_header_raw));
   BOOST_TEST(hex(protocol::signature_preimage(header)) == expected(spring::block_header_signature_preimage));
   BOOST_TEST(protocol::block_digest(header).str() == expected(spring::block_digest));
   BOOST_TEST(header.calculate_block_num() == spring::block_num_from_id);
   BOOST_TEST(protocol::calculate_block_num(header) == spring::block_num_from_id);
   BOOST_TEST(protocol::calculate_block_id(header).str() == expected(spring::block_id));
   BOOST_TEST(protocol::calculate_block_num_from_id(protocol::calculate_block_id(header)) == spring::block_num_from_id);

   const auto signature = parse_spring_signature(spring::block_signature);
   const auto recovered = forge::crypto::asymmetric::recover(signature, protocol::calculate_block_id(header));
   BOOST_TEST(format_spring_public_key(recovered) == expected(spring::test_public_key));

   const auto signed_header = make_reference_signed_block_header();
   BOOST_TEST(pack_hex(signed_header) == expected(spring::signed_block_header_raw));

   const auto receipt = make_reference_receipt();
   BOOST_TEST(pack_hex(receipt) == expected(spring::transaction_receipt_raw));
   BOOST_TEST(protocol::transaction_receipt_digest(receipt).str() == expected(spring::transaction_receipt_digest));

   const auto block = make_reference_signed_block();
   BOOST_TEST(pack_hex(block) == expected(spring::signed_block_raw));
}

BOOST_AUTO_TEST_CASE(transaction_mroot_uses_core_merkle_over_receipt_digests) {
   auto receipts = std::deque<protocol::transaction_receipt>{};
   BOOST_TEST(protocol::calculate_transaction_mroot(receipts) == core::digest{});

   receipts.push_back(make_receipt(1U));
   BOOST_TEST(protocol::calculate_transaction_mroot(receipts) == receipts.front().digest());

   receipts.push_back(make_receipt(2U));
   receipts.push_back(make_receipt(3U));
   const auto digests = std::array{
       receipts[0].digest(),
       receipts[1].digest(),
       receipts[2].digest(),
   };
   BOOST_TEST(protocol::calculate_transaction_mroot(receipts) == core::calculate_merkle_root(digests));
}

BOOST_AUTO_TEST_CASE(named_action_payload_owns_name_and_raw_bytes) {
   const auto permission = protocol::permission_level{
       .actor = protocol::make_name("alice"),
       .permission = protocol::make_name("storlane"),
   };
   const auto account = protocol::make_name("storlane");
   const auto payload = named_action_payload{.workspace = 41U, .inode = 73U};
   const auto action = protocol::action{permission, account, payload};

   BOOST_TEST(action.account.value == account.value);
   BOOST_TEST(action.name.value == named_action_payload::get_name().value);
   BOOST_TEST(action.authorization.size() == 1U);
   BOOST_TEST(action.authorization.front().actor.value == permission.actor.value);
   BOOST_TEST(action.authorization.front().permission.value == permission.permission.value);
   BOOST_TEST(action.data == forge::raw::pack(payload));
}

BOOST_AUTO_TEST_CASE(supported_protocol_features_have_a_typed_variant_contract) {
   const auto value = protocol::supported_protocol_features_response{
       .features =
           {
               {
                   .feature_digest =
                       protocol::digest{"0ec7e080177b2c02b278d5088611686b49d739925a92d9bfcacd7fc6b74053bd"},
                   .subjective_restrictions =
                       {
                           .enabled = true,
                           .preactivation_required = false,
                           .earliest_allowed_activation_time = protocol::time_point{},
                       },
                   .description_digest =
                       protocol::digest{"64fe7df32e9b86be2b296b3f81dfd527f84e82b98e363bc97e40bc7a83733310"},
                   .protocol_feature_type = "builtin",
                   .specification = {{.name = "builtin_feature_codename", .value = "PREACTIVATE_FEATURE"}},
               },
           },
   };

   auto encoded = forge::variant{};
   forge::to_variant(value, encoded);
   auto decoded = protocol::supported_protocol_features_response{};
   forge::from_variant(encoded, decoded);

   BOOST_CHECK(decoded == value);
}

BOOST_AUTO_TEST_CASE(forge_secp256k1_is_the_crypto_surface_for_runtime_signatures) {
   const auto private_key = forge::crypto::asymmetric::private_key::generate();
   const auto digest = forge::crypto::digest::sha256{std::string{spring::transaction_signature_digest}};
   const auto signature = private_key.sign_digest(digest);
   const auto public_key = private_key.get_public_key();
   const auto recovered_key = forge::crypto::asymmetric::recover(signature, digest);
   const auto signature_text = forge::crypto::asymmetric::encoding::forge().format(signature);
   const auto public_key_text = forge::crypto::asymmetric::encoding::forge().format(public_key);
   const auto signature_bytes = forge::raw::pack(signature);
   const auto public_key_bytes = forge::raw::pack(public_key);
   const auto parsed_signature = forge::crypto::asymmetric::encoding::forge().parse_signature(signature_text);
   const auto parsed_public_key = forge::crypto::asymmetric::encoding::forge().parse_public(public_key_text);
   const auto unpacked_signature = forge::raw::unpack<forge::crypto::asymmetric::signature>(signature_bytes);
   const auto unpacked_public_key = forge::raw::unpack<forge::crypto::asymmetric::public_key>(public_key_bytes);

   BOOST_TEST(static_cast<int>(forge::crypto::asymmetric::type(signature)) ==
              static_cast<int>(forge::crypto::asymmetric::algorithm::secp256k1));
   BOOST_TEST(recovered_key == public_key);
   BOOST_TEST(forge::crypto::asymmetric::encoding::forge().format(parsed_signature) == signature_text);
   BOOST_TEST(forge::crypto::asymmetric::encoding::forge().format(parsed_public_key) == public_key_text);
   BOOST_TEST(forge::crypto::asymmetric::encoding::forge().format(unpacked_signature) == signature_text);
   BOOST_TEST(forge::crypto::asymmetric::encoding::forge().format(unpacked_public_key) == public_key_text);

   const auto spring_public_key = parse_spring_public_key(spring::test_public_key);
   BOOST_TEST(format_spring_public_key(spring_public_key) == expected(spring::test_public_key));
}

BOOST_AUTO_TEST_SUITE_END()
