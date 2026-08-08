#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <cstdint>

import forge.chain.protocol.state_query;
import forge.chain.protocol.transaction_query;
import forge.codec.json;
import forge.crypto.digest.sha256;
import forge.raw.exceptions;
import forge.raw.raw;

namespace protocol = forge::chain::protocol;

BOOST_AUTO_TEST_CASE(transaction_submission_deadlines_are_canonical_raw_fields) {
   auto request = protocol::transaction_submit_request{.retry = true, .retry_blocks = 7U, .timeout_ms = 12'345U};
   const auto encoded = forge::raw::pack(request);
   const auto timeout = forge::raw::pack(request.timeout_ms);
   BOOST_REQUIRE(encoded.size() >= timeout.size());
   BOOST_TEST((protocol::bytes{encoded.end() - static_cast<std::ptrdiff_t>(timeout.size()), encoded.end()} == timeout));

   const auto decoded = forge::raw::unpack_exact<protocol::transaction_submit_request>(encoded);
   BOOST_TEST(decoded.retry);
   BOOST_REQUIRE(decoded.retry_blocks.has_value());
   BOOST_TEST(*decoded.retry_blocks == 7U);
   BOOST_TEST(decoded.timeout_ms == 12'345U);

   auto legacy = encoded;
   legacy.resize(legacy.size() - timeout.size());
   BOOST_CHECK_THROW((void)forge::raw::unpack_exact<protocol::transaction_submit_request>(legacy),
                     forge::raw::exceptions::range_error);

   const auto batch = protocol::transaction_submit_batch_request{
       .transactions = {request},
       .timeout_ms = 20'000U,
   };
   const auto batch_decoded =
       forge::raw::unpack_exact<protocol::transaction_submit_batch_request>(forge::raw::pack(batch));
   BOOST_REQUIRE(batch_decoded.transactions.size() == 1U);
   BOOST_TEST(batch_decoded.transactions.front().timeout_ms == request.timeout_ms);
   BOOST_TEST(batch_decoded.timeout_ms == batch.timeout_ms);

   const auto json = forge::codec::json::write(batch);
   BOOST_REQUIRE(json.ok());
   const auto json_value = forge::codec::json::read_value(json.text);
   BOOST_REQUIRE(json_value.ok());
   BOOST_TEST(json_value.value["timeout_ms"].as_uint64() == batch.timeout_ms);
   BOOST_TEST(json_value.value["transactions"][std::size_t{0}]["timeout_ms"].as_uint64() == request.timeout_ms);
}

BOOST_AUTO_TEST_CASE(table_rows_roundtrip_canonical_binary_contract) {
   const auto request = protocol::table_rows_request{
       .code = protocol::account_name{"eosio.token"},
       .scope = protocol::name{"alice"},
       .table = protocol::name{"accounts"},
       .index = {.kind = protocol::table_index_kind::secondary_u128, .position = 2U},
       .lower_bound = protocol::bytes{0x00U, 0x01U},
       .upper_bound = protocol::bytes{0xfeU, 0xffU},
       .cursor = protocol::bytes{0x10U, 0x20U},
       .limit = 25U,
       .reverse = true,
       .audit = protocol::audit_mode::required,
   };
   const auto encoded = forge::codec::json::write(request);
   BOOST_REQUIRE(encoded.ok());
   const auto decoded = forge::codec::json::read<protocol::table_rows_request>(
       encoded.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(decoded.ok());
   BOOST_CHECK(decoded.value == request);

   auto response = protocol::table_rows_response{};
   response.rows = {{.value = {0xdeU, 0xadU}, .payer = protocol::account_name{"eosio.token"}}};
   response.next = protocol::bytes{0x30U, 0x40U};
   const auto response_json = forge::codec::json::write(response);
   BOOST_REQUIRE(response_json.ok());
   const auto response_value = forge::codec::json::read_value(response_json.text);
   BOOST_REQUIRE(response_value.ok());
   BOOST_TEST(response_value.value["rows"][std::size_t{0}]["value"][std::size_t{0}].as_uint64() == 0xdeU);
   BOOST_TEST(response_value.value["next"][std::size_t{1}].as_uint64() == 0x40U);
}

BOOST_AUTO_TEST_CASE(table_scope_pagination_roundtrips_opaque_bytes_in_exact_json) {
   auto request = protocol::table_scope_request{
       .code = protocol::account_name{"eosio.token"},
       .table = protocol::name{"accounts"},
       .lower_bound = "alice",
       .upper_bound = "zebra",
       .limit = 25U,
       .reverse = true,
       .cursor = protocol::bytes{0x00U, 0x2fU, 0xffU},
       .audit = protocol::audit_mode::required,
   };
   const auto request_json = forge::codec::json::write(request);
   BOOST_REQUIRE(request_json.ok());
   const auto request_value = forge::codec::json::read_value(request_json.text);
   BOOST_REQUIRE(request_value.ok());
   const auto& cursor = request_value.value["cursor"].get_array();
   BOOST_REQUIRE_EQUAL(cursor.size(), 3U);
   BOOST_TEST(cursor[0].as_uint64() == 0U);
   BOOST_TEST(cursor[1].as_uint64() == 0x2fU);
   BOOST_TEST(cursor[2].as_uint64() == 0xffU);

   const auto exact = forge::codec::json::read<protocol::table_scope_request>(
       request_json.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(exact.ok());
   BOOST_CHECK(exact.value == request);

   auto response = protocol::table_scope_response{};
   response.rows = {{
       .code = protocol::name{"eosio.token"},
       .scope = protocol::name{"alice"},
       .table = protocol::name{"accounts"},
       .payer = protocol::account_name{"eosio.token"},
       .count = 1U,
   }};
   response.next = protocol::bytes{0x01U, 0x02U, 0x03U};
   const auto response_json = forge::codec::json::write(response);
   BOOST_REQUIRE(response_json.ok());
   const auto response_value = forge::codec::json::read_value(response_json.text);
   BOOST_REQUIRE(response_value.ok());
   const auto& object = response_value.value.get_object();
   BOOST_TEST(object.contains("next"));
   BOOST_TEST(!object.contains("more"));
   BOOST_TEST(!object.contains("next_key"));

   const auto exact_response = forge::codec::json::read<protocol::table_scope_response>(
       response_json.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(exact_response.ok());
   BOOST_CHECK(exact_response.value == response);
}

BOOST_AUTO_TEST_CASE(transaction_trace_is_one_typed_protocol_record_across_api_surfaces) {
   auto trace = protocol::transaction_trace{};
   trace.id._hash[0] = 0x42U;
   trace.cpu_usage_us = 17U;
   trace.net_usage = 32U;
   trace.actions.push_back({
       .action = protocol::action{{protocol::permission_level{protocol::account_name{"alice"},
                                                              protocol::permission_name{"active"}}},
                                  protocol::account_name{"eosio.token"},
                                  protocol::action_name{"transfer"},
                                  protocol::bytes{0x01U, 0x02U}},
       .receipt = protocol::action_receipt{.receiver = protocol::account_name{"eosio.token"}},
       .console = "executed",
   });
   trace.error = protocol::transaction_error{
       .category = "test",
       .code = 7,
       .message = "typed failure",
   };

   const auto encoded = forge::codec::json::write(trace);
   BOOST_REQUIRE(encoded.ok());
   const auto decoded = forge::codec::json::read<protocol::transaction_trace>(
       encoded.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(decoded.ok());
   BOOST_CHECK(decoded.value == trace);

   auto submitted = protocol::transaction_submit_response{.id = trace.id, .trace = trace};
   auto status = protocol::transaction_status_response{.id = trace.id, .trace = trace};
   auto read_only = protocol::transaction_read_only_response{.id = trace.id, .trace = trace};
   BOOST_CHECK(submitted.trace == status.trace);
   BOOST_CHECK(read_only.trace == *submitted.trace);
}
