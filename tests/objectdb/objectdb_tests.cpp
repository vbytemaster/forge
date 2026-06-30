#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import forge.objectdb.cursor;
import forge.objectdb.exceptions;
import forge.objectdb.index;
import forge.objectdb.key;
import forge.objectdb.record;
import forge.objectdb.types;

namespace {

std::string hex(std::span<const std::byte> value) {
   constexpr auto digits = std::string_view{"0123456789abcdef"};
   auto out = std::string{};
   out.reserve(value.size() * 2U);
   for (const auto byte : value) {
      const auto current = std::to_integer<unsigned>(byte);
      out.push_back(digits[(current >> 4U) & 0x0fU]);
      out.push_back(digits[current & 0x0fU]);
   }
   return out;
}

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
   auto out = std::vector<std::byte>{};
   out.reserve(values.size());
   for (const auto value : values) {
      out.push_back(static_cast<std::byte>(value));
   }
   return out;
}

struct objectdb_test_object {};
using objectdb_test_schema =
   forge::objectdb::schema<objectdb_test_object, forge::objectdb::table<7>,
                           forge::objectdb::index<1, forge::objectdb::index_kind::secondary_unique>>;

static_assert(forge::objectdb::object_schema<objectdb_test_schema>);
static_assert(objectdb_test_schema::table.value == 7U);
static_assert(objectdb_test_schema::index_count == 1U);

} // namespace

BOOST_AUTO_TEST_SUITE(objectdb_test_suite)

BOOST_AUTO_TEST_CASE(objectdb_object_and_index_keys_have_stable_ordered_bytes) {
   const auto object_key = forge::objectdb::make_object_key({1}, {2});
   BOOST_CHECK_EQUAL(hex(object_key), "10000000010000000000000002");

   const auto encoded_value = forge::objectdb::key_builder{}.append_ordered_text("name").finish();
   const auto index_key = forge::objectdb::make_secondary_index_key({1}, {3}, encoded_value, {2});
   BOOST_CHECK_EQUAL(hex(index_key), "2000000001000000036e616d6500000000000000000002");
}

BOOST_AUTO_TEST_CASE(objectdb_ordered_key_fragments_preserve_sort_order) {
   const auto negative = forge::objectdb::key_builder{}.append_i64(-1).finish();
   const auto zero = forge::objectdb::key_builder{}.append_i64(0).finish();
   const auto positive = forge::objectdb::key_builder{}.append_i64(1).finish();

   BOOST_CHECK(negative < zero);
   BOOST_CHECK(zero < positive);

   const auto empty = forge::objectdb::key_builder{}.append_ordered_bytes({}).finish();
   const auto zero_byte = forge::objectdb::key_builder{}.append_ordered_bytes(bytes({0})).finish();
   const auto one_byte = forge::objectdb::key_builder{}.append_ordered_bytes(bytes({1})).finish();

   BOOST_CHECK_EQUAL(hex(empty), "0000");
   BOOST_CHECK_EQUAL(hex(zero_byte), "00ff0000");
   BOOST_CHECK(empty < zero_byte);
   BOOST_CHECK(zero_byte < one_byte);
}

BOOST_AUTO_TEST_CASE(objectdb_prefix_range_is_bounded_when_possible) {
   const auto prefix = forge::objectdb::make_object_prefix({1});
   const auto range = forge::objectdb::prefix_range(prefix);

   BOOST_CHECK(range.has_end);
   BOOST_CHECK_EQUAL(hex(range.begin), "1000000001");
   BOOST_CHECK_EQUAL(hex(range.end), "1000000002");

   const auto unbounded = forge::objectdb::prefix_range(bytes({0xff, 0xff}));
   BOOST_CHECK(!unbounded.has_end);
   BOOST_CHECK_EQUAL(hex(unbounded.begin), "ffff");
}

BOOST_AUTO_TEST_CASE(objectdb_cursor_is_opaque_key_boundary_with_checked_limit) {
   const auto last_key = forge::objectdb::make_object_key({9}, {44});
   const auto cursor = forge::objectdb::make_cursor(last_key);
   const auto window = forge::objectdb::make_page_window({.cursor = cursor, .limit = 32});

   BOOST_CHECK(window.start_after == last_key);
   BOOST_CHECK_EQUAL(window.limit, 32U);

   BOOST_CHECK_EXCEPTION(forge::objectdb::validate_page_limit(0), forge::objectdb::exceptions::invalid_cursor,
                         [](const forge::objectdb::exceptions::invalid_cursor& error) {
                            return error.code().category().name() == std::string_view{"forge.objectdb"};
                         });
}

BOOST_AUTO_TEST_CASE(objectdb_records_and_mutations_are_storage_neutral_values) {
   const auto put = forge::objectdb::mutation::put(bytes({1, 2}), bytes({3, 4}));
   BOOST_CHECK(put.kind == forge::objectdb::mutation_kind::put);
   BOOST_CHECK_EQUAL(hex(put.key), "0102");
   BOOST_CHECK_EQUAL(hex(put.value), "0304");

   const auto erase = forge::objectdb::mutation::erase(bytes({1, 2}));
   BOOST_CHECK(erase.kind == forge::objectdb::mutation_kind::erase);
   BOOST_CHECK_EQUAL(hex(erase.key), "0102");
   BOOST_CHECK(erase.value.empty());
}

BOOST_AUTO_TEST_SUITE_END()
