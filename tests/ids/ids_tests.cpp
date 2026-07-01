#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

import forge.ids.types;
import forge.raw.raw;
import forge.variant.value;

namespace {

std::vector<char> pack_object_id(forge::ids::object_id value) {
   return forge::raw::pack(value);
}

forge::ids::object_id unpack_object_id(const std::vector<char>& bytes) {
   return forge::raw::unpack<forge::ids::object_id>(bytes);
}

} // namespace

BOOST_AUTO_TEST_SUITE(ids_test_suite)

BOOST_AUTO_TEST_CASE(ids_object_id_raw_roundtrip_preserves_field_order) {
   const auto original = forge::ids::object_id{.space = 7, .type = 513, .instance = 0x0102030405060708ULL};
   const auto packed = pack_object_id(original);

   BOOST_REQUIRE_EQUAL(packed.size(), 11U);
   BOOST_CHECK_EQUAL(static_cast<unsigned char>(packed[0]), 7U);
   BOOST_CHECK_EQUAL(static_cast<unsigned char>(packed[1]), 1U);
   BOOST_CHECK_EQUAL(static_cast<unsigned char>(packed[2]), 2U);

   const auto decoded = unpack_object_id(packed);
   BOOST_CHECK(decoded == original);
}

BOOST_AUTO_TEST_CASE(ids_object_id_variant_roundtrip_preserves_object_shape) {
   const auto original = forge::ids::object_id{.space = 3, .type = 9, .instance = 42};
   auto encoded = forge::variant{};
   to_variant(original, encoded);

   auto decoded = forge::ids::object_id{};
   from_variant(encoded, decoded);

   BOOST_CHECK(decoded == original);
}

BOOST_AUTO_TEST_CASE(ids_typed_id_converts_to_and_from_object_id) {
   using account_id = forge::ids::typed_id<1, 2>;

   const auto typed = account_id{99};
   const auto generic = typed.as_object_id();

   BOOST_CHECK_EQUAL(generic.space, 1U);
   BOOST_CHECK_EQUAL(generic.type, 2U);
   BOOST_CHECK_EQUAL(generic.instance, 99U);
   BOOST_CHECK((forge::ids::matches<1, 2>(generic)));

   const auto roundtrip = account_id{generic};
   BOOST_CHECK(roundtrip == typed);

   const auto maybe_typed = forge::ids::try_typed<1, 2>(generic);
   BOOST_REQUIRE(maybe_typed.has_value());
   BOOST_CHECK(*maybe_typed == typed);
}

BOOST_AUTO_TEST_CASE(ids_typed_id_rejects_mismatched_space_or_type) {
   using account_id = forge::ids::typed_id<1, 2>;

   BOOST_CHECK_EXCEPTION(account_id(forge::ids::object_id{.space = 1, .type = 3, .instance = 99}), std::invalid_argument,
                         [](const std::invalid_argument& error) {
                            return std::string{error.what()} == "object_id space/type does not match typed_id";
                         });

   BOOST_CHECK((!forge::ids::try_typed<1, 2>(forge::ids::object_id{.space = 9, .type = 2, .instance = 99}).has_value()));
}

BOOST_AUTO_TEST_CASE(ids_to_string_uses_space_type_instance) {
   const auto generic = forge::ids::object_id{.space = 5, .type = 17, .instance = 1234};
   BOOST_CHECK_EQUAL(forge::ids::to_string(generic), "5/17/1234");

   const auto typed = forge::ids::typed_id<5, 17>{1234};
   BOOST_CHECK_EQUAL(forge::ids::to_string(typed), "5/17/1234");
}

BOOST_AUTO_TEST_SUITE_END()
