#include <boost/test/unit_test.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import forge.codec.json;
import forge.crypto.asymmetric.serialization;
import forge.variant.value;

namespace asymmetric = forge::crypto::asymmetric;

namespace {

template <typename T, std::size_t Size> [[nodiscard]] std::array<T, Size> sequence(std::uint8_t first) {
   auto result = std::array<T, Size>{};
   for (auto index = std::size_t{}; index < result.size(); ++index) {
      result[index] = static_cast<T>(first + index);
   }
   return result;
}

template <typename Value> void check_serialization_roundtrip(const Value& expected, std::string_view prefix) {
   auto encoded = forge::variant{};
   asymmetric::to_variant(expected, encoded);
   BOOST_REQUIRE(encoded.is_string());
   BOOST_CHECK(encoded.get_string().starts_with(prefix));

   auto decoded = Value{};
   asymmetric::from_variant(encoded, decoded);
   BOOST_CHECK(decoded == expected);

   const auto json = forge::codec::json::write(expected);
   BOOST_REQUIRE(json.ok());
   const auto parsed = forge::codec::json::read_value(json.text);
   BOOST_REQUIRE(parsed.ok());
   BOOST_REQUIRE(parsed.value.is_string());
   BOOST_CHECK(parsed.value.get_string().starts_with(prefix));

   const auto exact = forge::codec::json::read<Value>(
       json.text, {.described_records = forge::codec::json::described_record_policy::exact});
   for (const auto& diagnostic : exact.diagnostics) {
      BOOST_TEST_MESSAGE(diagnostic.code << " at " << diagnostic.path << ": " << diagnostic.message);
   }
   BOOST_REQUIRE(exact.ok());
   BOOST_CHECK(exact.value == expected);
}

} // namespace

BOOST_AUTO_TEST_SUITE(crypto_asymmetric_serialization)

BOOST_AUTO_TEST_CASE(public_key_variant_and_exact_json_roundtrip_all_algorithms) {
   const auto cases = std::vector<std::pair<asymmetric::public_key, std::string_view>>{
       {asymmetric::k1_public_key{sequence<char, 33>(1U)}, "PUB_SECP256K1_"},
       {asymmetric::r1_public_key{sequence<char, 33>(2U)}, "PUB_P256_"},
       {asymmetric::webauthn_public_key{sequence<char, 33>(3U),
                                        asymmetric::webauthn_public_key::user_presence_t::USER_PRESENCE_VERIFIED,
                                        "login.example"},
        "PUB_WEBAUTHN_"},
       {asymmetric::ed25519_public_key{sequence<std::uint8_t, 32>(4U)}, "PUB_ED25519_"},
       {asymmetric::rsa_public_key{{5U, 6U, 7U, 8U}}, "PUB_RSA_"},
   };

   for (const auto& [value, prefix] : cases) {
      check_serialization_roundtrip(value, prefix);
   }
}

BOOST_AUTO_TEST_CASE(signature_variant_and_exact_json_roundtrip_all_algorithms) {
   const auto cases = std::vector<std::pair<asymmetric::signature, std::string_view>>{
       {asymmetric::k1_signature{sequence<char, 65>(11U)}, "SIG_SECP256K1_"},
       {asymmetric::r1_signature{sequence<char, 65>(12U)}, "SIG_P256_"},
       {asymmetric::webauthn_signature{sequence<char, 65>(13U), {14U, 15U, 16U}, R"({"type":"webauthn.get"})"},
        "SIG_WEBAUTHN_"},
       {asymmetric::ed25519_signature{sequence<std::uint8_t, 64>(17U)}, "SIG_ED25519_"},
       {asymmetric::rsa_signature{{18U, 19U, 20U, 21U}}, "SIG_RSA_"},
   };

   for (const auto& [value, prefix] : cases) {
      check_serialization_roundtrip(value, prefix);
   }
}

BOOST_AUTO_TEST_SUITE_END()
