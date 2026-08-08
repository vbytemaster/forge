#include <boost/test/unit_test.hpp>

#include <concepts>
#include <optional>
#include <variant>

import forge.chain.protocol.block;
import forge.chain.protocol.finalizer_policy;
import forge.chain.protocol.producer_authority;
import forge.chain.protocol.producer_schedule;
import forge.codec.hex;
import forge.codec.json;
import forge.raw.raw;
import forge.variant.described;
import forge.variant.static_variant;
import forge.variant.value;

namespace protocol = forge::chain::protocol;

static_assert(
    std::same_as<decltype(protocol::block_header::new_producers), std::optional<protocol::producer_schedule>>);

BOOST_AUTO_TEST_SUITE(chain_protocol_producer_schedule_tests)

BOOST_AUTO_TEST_CASE(block_and_authority_modules_share_the_canonical_schedule_types) {
   const auto key = protocol::producer_key{
       .producer_name = {},
       .block_signing_key = protocol::public_key{std::in_place_index<0>},
   };
   const auto schedule = protocol::producer_schedule{
       .version = 7,
       .producers = {key},
   };

   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(key)) ==
              "000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(schedule)) ==
              "0700000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000");

   auto encoded = forge::variant{};
   forge::to_variant(schedule, encoded);

   auto decoded = protocol::producer_schedule{};
   forge::from_variant(encoded, decoded);
   BOOST_CHECK(decoded == schedule);

   const auto json = forge::codec::json::write(schedule);
   BOOST_REQUIRE(json.ok());
   const auto parsed_json = forge::codec::json::read_value(json.text);
   BOOST_REQUIRE(parsed_json.ok());
   const auto& json_signing_key =
       parsed_json.value.get_object()["producers"].get_array().front().get_object()["block_signing_key"];
   BOOST_REQUIRE(json_signing_key.is_string());
   BOOST_CHECK(json_signing_key.get_string().starts_with("PUB_SECP256K1_"));

   const auto exact = forge::codec::json::read<protocol::producer_schedule>(
       json.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(exact.ok());
   BOOST_CHECK(exact.value == schedule);
}

BOOST_AUTO_TEST_CASE(authority_records_use_spring_variant_shape_and_roundtrip) {
   const auto key = protocol::key_weight{
       .key = protocol::public_key{std::in_place_index<0>},
       .weight = 2U,
   };
   const auto schedule = protocol::producer_authority_schedule{
       .version = 7U,
       .producers = {protocol::producer_authority{
           .producer_name = {},
           .authority =
               protocol::block_signing_authority_v0{
                   .threshold = 2U,
                   .keys = {key},
               },
       }},
   };

   auto encoded = forge::variant{};
   forge::to_variant(schedule, encoded);
   const auto& authority = encoded.get_object()["producers"].get_array().front().get_object()["authority"].get_array();
   BOOST_REQUIRE_EQUAL(authority.size(), 2U);
   BOOST_TEST(authority[0].as_uint64() == 0U);
   BOOST_TEST(authority[1].get_object()["threshold"].as_uint64() == 2U);

   auto decoded = protocol::producer_authority_schedule{};
   forge::from_variant(encoded, decoded);
   BOOST_CHECK(decoded == schedule);

   const auto raw = forge::codec::hex::encode(forge::raw::pack(schedule));
   BOOST_TEST(raw ==
              "0700000001000000000000000000020000000100000000000000000000000000000000000000000000000000000000000000"
              "0000000200");

   const auto json = forge::codec::json::write(schedule, {.pretty = true});
   BOOST_REQUIRE(json.ok());
   const auto parsed_json = forge::codec::json::read_value(json.text);
   BOOST_REQUIRE(parsed_json.ok());
   const auto& json_authority =
       parsed_json.value.get_object()["producers"].get_array().front().get_object()["authority"].get_array();
   BOOST_REQUIRE_EQUAL(json_authority.size(), 2U);
   BOOST_TEST(json_authority[0].as_uint64() == 0U);
   const auto& json_authority_key = json_authority[1].get_object()["keys"].get_array().front().get_object()["key"];
   BOOST_REQUIRE(json_authority_key.is_string());
   BOOST_CHECK(json_authority_key.get_string().starts_with("PUB_SECP256K1_"));

   const auto exact = forge::codec::json::read<protocol::producer_authority_schedule>(
       json.text, {.described_records = forge::codec::json::described_record_policy::exact});
   for (const auto& diagnostic : exact.diagnostics) {
      BOOST_TEST_MESSAGE(diagnostic.code << " at " << diagnostic.path << ": " << diagnostic.message);
   }
   BOOST_REQUIRE(exact.ok());
   BOOST_CHECK(exact.value == schedule);

   const auto object_only = forge::codec::json::read<protocol::producer_authority_schedule>(
       R"({"version":7,"producers":[{"producer_name":"","authority":{"threshold":2,"keys":[]}}]})",
       {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(!object_only.ok());
   BOOST_TEST(object_only.diagnostics.front().code == "json.variant");
   BOOST_TEST(object_only.diagnostics.front().path == "producers[0].authority");

   const auto boolean_version = forge::codec::json::read<protocol::producer_authority_schedule>(
       R"({"version":true,"producers":[]})", {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(!boolean_version.ok());
   BOOST_TEST(boolean_version.diagnostics.front().code == "json.type");
   BOOST_TEST(boolean_version.diagnostics.front().path == "version");

   const auto overflowing_version = forge::codec::json::read<protocol::producer_authority_schedule>(
       R"({"version":4294967296,"producers":[]})",
       {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(!overflowing_version.ok());
   BOOST_TEST(overflowing_version.diagnostics.front().code == "json.range");
   BOOST_TEST(overflowing_version.diagnostics.front().path == "version");
}

BOOST_AUTO_TEST_CASE(finalizer_policy_has_canonical_equality_and_variant_roundtrip) {
   const auto policy = protocol::finalizer_policy{
       .threshold = 4U,
       .finalizers = {protocol::finalizer_authority{
           .description = "f",
           .weight = 3U,
           .public_key = {char{1}, char{2}},
       }},
   };

   auto encoded = forge::variant{};
   forge::to_variant(policy, encoded);
   auto decoded = protocol::finalizer_policy{};
   forge::from_variant(encoded, decoded);

   BOOST_CHECK(decoded == policy);
   BOOST_TEST(forge::codec::hex::encode(forge::raw::pack(policy)) == "04000000000000000101660300000000000000020102");

   const auto malformed_key = forge::codec::json::read<protocol::finalizer_policy>(
       R"({"threshold":4,"finalizers":[{"description":"f","weight":3,"public_key":"0"}]})",
       {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(!malformed_key.ok());
   BOOST_TEST(malformed_key.diagnostics.front().code == "json.type");
   BOOST_TEST(malformed_key.diagnostics.front().path == "finalizers[0].public_key");
}

BOOST_AUTO_TEST_SUITE_END()
