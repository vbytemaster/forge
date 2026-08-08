#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <ranges>
#include <variant>
#include <vector>

import forge.crypto.asymmetric.values;
import forge.raw.raw;

BOOST_AUTO_TEST_SUITE(crypto_asymmetric_values)

BOOST_AUTO_TEST_CASE(binary_values_keep_algorithm_and_raw_contracts) {
   using namespace forge::crypto::asymmetric;

   static_assert(static_cast<std::int32_t>(algorithm::secp256k1) == 0);
   static_assert(static_cast<std::int32_t>(algorithm::p256) == 1);
   static_assert(static_cast<std::int32_t>(algorithm::webauthn) == 2);
   static_assert(static_cast<std::int32_t>(algorithm::ed25519) == 3);
   static_assert(static_cast<std::int32_t>(algorithm::rsa) == 4);

   auto data = ecc_public_key{};
   data.front() = 2;
   const auto key = public_key{k1_public_key{data}};
   const auto packed = forge::raw::pack(key);
   const auto unpacked = forge::raw::unpack<public_key>(packed);

   BOOST_CHECK(type(key) == algorithm::secp256k1);
   BOOST_CHECK(std::get<k1_public_key>(unpacked).data == data);
}

BOOST_AUTO_TEST_CASE(canonical_values_order_by_algorithm_then_payload) {
   using namespace forge::crypto::asymmetric;

   auto first_data = ecc_public_key{};
   first_data.front() = 2;
   auto second_data = first_data;
   second_data.back() = 1;

   const auto first = public_key{k1_public_key{first_data}};
   const auto second = public_key{k1_public_key{second_data}};
   const auto r1 = public_key{r1_public_key{first_data}};

   BOOST_CHECK((first <=> second) == std::strong_ordering::less);
   BOOST_CHECK((second <=> first) == std::strong_ordering::greater);
   BOOST_CHECK((first <=> first) == std::strong_ordering::equal);
   BOOST_CHECK((second <=> r1) == std::strong_ordering::less);

   auto first_signature_data = ecc_signature{};
   auto second_signature_data = first_signature_data;
   second_signature_data.back() = 1;
   const auto first_signature = signature{k1_signature{first_signature_data}};
   const auto second_signature = signature{k1_signature{second_signature_data}};
   const auto r1_signature_value = signature{r1_signature{first_signature_data}};

   BOOST_CHECK((first_signature <=> second_signature) == std::strong_ordering::less);
   BOOST_CHECK((second_signature <=> r1_signature_value) == std::strong_ordering::less);

   auto keys = std::vector<public_key>{r1, second, first};
   std::ranges::sort(keys);
   BOOST_CHECK(keys == (std::vector<public_key>{first, second, r1}));
}

BOOST_AUTO_TEST_CASE(canonical_ecc_payload_order_treats_char_storage_as_unsigned_bytes) {
   using namespace forge::crypto::asymmetric;

   auto lower_key = ecc_public_key{};
   auto higher_key = ecc_public_key{};
   lower_key.front() = static_cast<char>(0x7fU);
   higher_key.front() = static_cast<char>(0x80U);

   BOOST_CHECK((k1_public_key{lower_key} <=> k1_public_key{higher_key}) == std::strong_ordering::less);
   BOOST_CHECK((r1_public_key{lower_key} <=> r1_public_key{higher_key}) == std::strong_ordering::less);
   BOOST_CHECK((webauthn_public_key{lower_key, webauthn_public_key::user_presence_t::USER_PRESENCE_PRESENT, "rp"} <=>
                webauthn_public_key{higher_key, webauthn_public_key::user_presence_t::USER_PRESENCE_PRESENT, "rp"}) ==
               std::strong_ordering::less);
   BOOST_CHECK((webauthn_public_key{lower_key, webauthn_public_key::user_presence_t::USER_PRESENCE_NONE, "rp"} <=>
                webauthn_public_key{lower_key, webauthn_public_key::user_presence_t::USER_PRESENCE_PRESENT, "rp"}) ==
               std::strong_ordering::less);
   BOOST_CHECK((webauthn_public_key{lower_key, webauthn_public_key::user_presence_t::USER_PRESENCE_PRESENT, "a"} <=>
                webauthn_public_key{lower_key, webauthn_public_key::user_presence_t::USER_PRESENCE_PRESENT, "b"}) ==
               std::strong_ordering::less);

   auto lower_signature = ecc_signature{};
   auto higher_signature = ecc_signature{};
   lower_signature.front() = static_cast<char>(0x7fU);
   higher_signature.front() = static_cast<char>(0x80U);

   BOOST_CHECK((k1_signature{lower_signature} <=> k1_signature{higher_signature}) == std::strong_ordering::less);
   BOOST_CHECK((r1_signature{lower_signature} <=> r1_signature{higher_signature}) == std::strong_ordering::less);
   BOOST_CHECK((webauthn_signature{lower_signature, {}, "client"} <=>
                webauthn_signature{higher_signature, {}, "client"}) == std::strong_ordering::less);
   BOOST_CHECK((webauthn_signature{lower_signature, {0x7fU}, "client"} <=>
                webauthn_signature{lower_signature, {0x80U}, "client"}) == std::strong_ordering::less);
   BOOST_CHECK((webauthn_signature{lower_signature, {0x7fU}, "a"} <=>
                webauthn_signature{lower_signature, {0x7fU}, "b"}) == std::strong_ordering::less);
}

BOOST_AUTO_TEST_SUITE_END()
