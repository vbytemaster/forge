#include <boost/test/unit_test.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

import forge.ids.types;
import forge.objectdb.cursor;
import forge.objectdb.descriptor;
import forge.objectdb.exceptions;
import forge.objectdb.layout;
import forge.objectdb.types;

namespace {

struct account {
   forge::ids::typed_id<1, 7> id;
   std::string name;
   std::uint64_t balance = 0;
   std::uint32_t region = 0;
};

struct by_id;
struct by_name;
struct by_region_balance;

using account_object = forge::objectdb::object<
   account,
   forge::objectdb::indexed_by<
      forge::objectdb::primary_unique<by_id, &account::id>,
      forge::objectdb::secondary_unique<by_name, &account::name>,
      forge::objectdb::secondary_non_unique<
         by_region_balance,
         forge::objectdb::composite_key<&account::region, &account::balance>>>>;

struct bad_primary_account {
   std::uint64_t id = 0;
};

struct bad_primary_tag;
using bad_primary_object =
   forge::objectdb::object<bad_primary_account,
                           forge::objectdb::indexed_by<
                              forge::objectdb::primary_unique<bad_primary_tag, &bad_primary_account::id>>>;

struct no_primary_tag;
using no_primary_object =
   forge::objectdb::object<account,
                           forge::objectdb::indexed_by<
                              forge::objectdb::secondary_unique<no_primary_tag, &account::name>>>;

using duplicate_tag_object =
   forge::objectdb::object<account,
                           forge::objectdb::indexed_by<
                              forge::objectdb::primary_unique<by_id, &account::id>,
                              forge::objectdb::secondary_unique<by_id, &account::name>>>;

std::string hex(const std::vector<std::byte>& bytes) {
   auto out = std::ostringstream{};
   out << std::hex << std::setfill('0');
   for (auto byte : bytes) {
      out << std::setw(2) << std::to_integer<unsigned>(byte);
   }
   return out.str();
}

} // namespace

static_assert(forge::objectdb::object_model<account_object>);
static_assert(!forge::objectdb::object_model<bad_primary_object>);
static_assert(!forge::objectdb::object_model<no_primary_object>);
static_assert(!forge::objectdb::object_model<duplicate_tag_object>);
static_assert(std::same_as<forge::objectdb::id_type_of<account_object>, forge::ids::typed_id<1, 7>>);
static_assert(std::same_as<forge::objectdb::index_by_tag<account_object, by_name>,
                           forge::objectdb::secondary_unique<by_name, &account::name>>);
static_assert(forge::objectdb::index_id_by_tag<account_object, by_id>.value == 0);
static_assert(forge::objectdb::index_id_by_tag<account_object, by_name>.value == 1);
static_assert(forge::objectdb::index_id_by_tag<account_object, by_region_balance>.value == 2);

BOOST_AUTO_TEST_SUITE(objectdb_test_suite)

BOOST_AUTO_TEST_CASE(objectdb_descriptor_derives_type_from_typed_id_primary) {
   constexpr auto type = forge::objectdb::object_type_of<account_object>::value;

   BOOST_CHECK_EQUAL(type.space, 1U);
   BOOST_CHECK_EQUAL(type.type, 7U);
}

BOOST_AUTO_TEST_CASE(objectdb_layout_encodes_object_record_key_from_id) {
   using layout = forge::objectdb::layout<account_object>;

   const auto key = layout::object_record_key(forge::ids::typed_id<1, 7>{42});

   BOOST_CHECK_EQUAL(hex(key.bytes()), "10010007000000000000002a");
}

BOOST_AUTO_TEST_CASE(objectdb_layout_encodes_unique_secondary_index_key) {
   using layout = forge::objectdb::layout<account_object>;

   const auto value = account{.id = forge::ids::typed_id<1, 7>{42}, .name = "alice", .balance = 100, .region = 3};
   const auto key = layout::index_entry_key<by_name>(value);

   BOOST_CHECK_EQUAL(hex(key.bytes()), "2001000700000001616c69636500");
}

BOOST_AUTO_TEST_CASE(objectdb_layout_encodes_non_unique_composite_index_key_with_primary_tiebreaker) {
   using layout = forge::objectdb::layout<account_object>;

   const auto value = account{.id = forge::ids::typed_id<1, 7>{42}, .name = "alice", .balance = 100, .region = 3};
   const auto key = layout::index_entry_key<by_region_balance>(value);

   BOOST_CHECK_EQUAL(hex(key.bytes()), "2101000700000002000000030000000000000064000000000000002a");
}

BOOST_AUTO_TEST_CASE(objectdb_layout_builds_partial_composite_prefix_range) {
   using layout = forge::objectdb::layout<account_object>;

   const auto range = layout::index_prefix<by_region_balance>(std::uint32_t{3});

   BOOST_CHECK_EQUAL(hex(range.begin.bytes()), "210100070000000200000003");
   BOOST_REQUIRE(range.has_end);
   BOOST_CHECK_EQUAL(hex(range.end.bytes()), "210100070000000200000004");
}

BOOST_AUTO_TEST_CASE(objectdb_cursor_is_opaque_key_boundary) {
   using layout = forge::objectdb::layout<account_object>;

   const auto key = layout::object_record_key(forge::ids::typed_id<1, 7>{42});
   const auto cursor = forge::objectdb::cursor{.boundary = key};

   BOOST_CHECK(cursor.boundary == key);
}

BOOST_AUTO_TEST_CASE(objectdb_page_request_rejects_invalid_limits_with_typed_exception) {
   BOOST_CHECK_THROW(forge::objectdb::validate_page_request(forge::objectdb::page_request{.limit = 0}),
                     forge::objectdb::exceptions::invalid_cursor);
   BOOST_CHECK_THROW(forge::objectdb::validate_page_request(
                        forge::objectdb::page_request{.limit = forge::objectdb::max_page_limit + 1}),
                     forge::objectdb::exceptions::invalid_cursor);
   BOOST_CHECK_NO_THROW(forge::objectdb::validate_page_request(forge::objectdb::page_request{.limit = 100}));
}

BOOST_AUTO_TEST_SUITE_END()
