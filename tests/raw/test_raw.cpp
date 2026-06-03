#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <fcl/raw/serialization.hpp>
#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import fcl.exceptions;
import fcl.crypto.hex;
import fcl.crypto.sha256;
import fcl.raw.datastream;
import fcl.raw.exceptions;
import fcl.raw.raw;
import fcl.variant;
import fcl.variant.dynamic_bitset;

using namespace fcl;

struct A {
   int x;
   float y;
   std::optional<std::string> z;

   bool operator==(const A&) const = default;
};
BOOST_DESCRIBE_STRUCT(A, (), (x, y, z))

enum class described_mode : int64_t { read = 7, write = 9 };
BOOST_DESCRIBE_ENUM(described_mode, read, write)

struct described_base {
   uint16_t parent = 0;

   bool operator==(const described_base&) const = default;
};
BOOST_DESCRIBE_STRUCT(described_base, (), (parent))

struct described_child : described_base {
   uint8_t child = 0;

   bool operator==(const described_child&) const = default;
};
BOOST_DESCRIBE_STRUCT(described_child, (described_base), (child))

struct macro_serialized_record {
   uint16_t id = 0;
   std::string name;

   bool operator==(const macro_serialized_record&) const = default;
};
BOOST_DESCRIBE_STRUCT(macro_serialized_record, (), (id, name))

FCL_DECLARE_SERIALIZATION(macro_serialized_record)
FCL_IMPLEMENT_SERIALIZATION(macro_serialized_record)

BOOST_AUTO_TEST_SUITE(raw_test_suite)

BOOST_AUTO_TEST_CASE(raw_string_golden_bytes) {
   const auto packed = fcl::raw::pack(std::string("abc"));
   BOOST_CHECK_EQUAL(fcl::crypto::to_hex(packed), "03616263");
}

BOOST_AUTO_TEST_CASE(boost_describe_struct_preserves_fc_reflect_member_order) {
   const A value{2, 2.25f, std::string("abc")};
   const auto packed = fcl::raw::pack(value);

   BOOST_CHECK_EQUAL(fcl::crypto::to_hex(packed), "02000000000010400103616263");

   const auto unpacked = fcl::raw::unpack<A>(packed);
   BOOST_CHECK(value == unpacked);
}

BOOST_AUTO_TEST_CASE(boost_describe_enum_uses_old_reflected_enum_int64_layout) {
   const auto packed = fcl::raw::pack(described_mode::write);

   BOOST_CHECK_EQUAL(fcl::crypto::to_hex(packed), "0900000000000000");

   const auto unpacked = fcl::raw::unpack<described_mode>(packed);
   BOOST_CHECK(unpacked == described_mode::write);
}

BOOST_AUTO_TEST_CASE(boost_describe_derived_types_pack_base_first_then_local_members) {
   described_child value;
   value.parent = 0x1234;
   value.child = 0x56;

   const auto packed = fcl::raw::pack(value);

   BOOST_CHECK_EQUAL(fcl::crypto::to_hex(packed), "341256");

   const auto unpacked = fcl::raw::unpack<described_child>(packed);
   BOOST_CHECK(value == unpacked);
}

BOOST_AUTO_TEST_CASE(serialization_macros_instantiate_raw_variant_and_digest_pack_paths) {
   const macro_serialized_record value{0x1234, "node"};

   fcl::variant variant_value;
   fcl::to_variant(value, variant_value);
   auto from_variant = macro_serialized_record{};
   fcl::from_variant(variant_value, from_variant);
   BOOST_CHECK(value == from_variant);

   fcl::datastream<size_t> size_stream;
   fcl::raw::pack(size_stream, value);
   BOOST_CHECK_EQUAL(size_stream.tellp(), 7u);

   char buffer[32]{};
   fcl::datastream<char*> write_stream(buffer, sizeof(buffer));
   fcl::raw::pack(write_stream, value);
   BOOST_CHECK_EQUAL(fcl::crypto::to_hex(std::vector<char>(buffer, buffer + write_stream.tellp())), "3412046e6f6465");

   fcl::datastream<const char*> read_stream(buffer, write_stream.tellp());
   auto unpacked = macro_serialized_record{};
   fcl::raw::unpack(read_stream, unpacked);
   BOOST_CHECK(value == unpacked);

   fcl::crypto::sha256::encoder digest_stream;
   fcl::raw::pack(digest_stream, value);
   BOOST_CHECK_EQUAL(digest_stream.result().str(),
                     fcl::crypto::sha256::hash(buffer, static_cast<uint32_t>(write_stream.tellp())).str());
}

BOOST_AUTO_TEST_CASE(raw_can_pack_into_uint8_byte_containers_without_changing_legacy_char_pack) {
   const macro_serialized_record value{0x1234, "node"};

   const auto legacy = fcl::raw::pack(value);
   auto bytes = std::vector<std::uint8_t>{};
   fcl::raw::pack(bytes, value);

   BOOST_CHECK_EQUAL(fcl::crypto::to_hex(std::vector<char>{bytes.begin(), bytes.end()}), fcl::crypto::to_hex(legacy));
   BOOST_CHECK(fcl::raw::unpack<macro_serialized_record>(bytes) == value);

   const auto view = std::span<const std::uint8_t>{bytes.data(), bytes.size()};
   BOOST_CHECK(fcl::raw::unpack<macro_serialized_record>(view) == value);
}

BOOST_AUTO_TEST_CASE(unknown_variant_wire_type_throws_codec_error) {
   const std::vector<std::uint8_t> invalid_variant{0xff};
   BOOST_CHECK_EXCEPTION((void)fcl::raw::unpack<fcl::variant>(invalid_variant), fcl::raw::exceptions::codec_error,
                         [](const fcl::raw::exceptions::codec_error& error) {
      return error.code().category().name() == std::string_view{"fcl.raw"};
   });
}

BOOST_AUTO_TEST_CASE(std_chrono_preserves_old_fc_raw_layout) {
   using sys_time_us = std::chrono::sys_time<std::chrono::microseconds>;

   BOOST_CHECK_EQUAL(fcl::crypto::to_hex(fcl::raw::pack(sys_time_us{})), "0000000000000000");
   BOOST_CHECK_EQUAL(fcl::crypto::to_hex(fcl::raw::pack(sys_time_us{std::chrono::seconds{1}})), "40420f0000000000");
   BOOST_CHECK_EQUAL(fcl::crypto::to_hex(fcl::raw::pack(std::chrono::sys_seconds{std::chrono::seconds{1}})), "01000000");
   BOOST_CHECK_EQUAL(fcl::crypto::to_hex(fcl::raw::pack(std::chrono::microseconds{-1})), "ffffffffffffffff");

   BOOST_CHECK(fcl::raw::unpack<sys_time_us>(fcl::raw::pack(sys_time_us{std::chrono::seconds{1}})) ==
               sys_time_us{std::chrono::seconds{1}});
   BOOST_CHECK(fcl::raw::unpack<std::chrono::sys_seconds>(fcl::raw::pack(std::chrono::sys_seconds{
                   std::chrono::seconds{1}})) == std::chrono::sys_seconds{std::chrono::seconds{1}});
   BOOST_CHECK(fcl::raw::unpack<std::chrono::microseconds>(fcl::raw::pack(std::chrono::microseconds{-1})) ==
               std::chrono::microseconds{-1});
   BOOST_CHECK_THROW(fcl::raw::pack(std::chrono::sys_seconds{std::chrono::seconds{-1}}), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(dynamic_bitset_test) {
   constexpr uint8_t bits = 0b00011110;
   fcl::dynamic_bitset bs1(8, bits); // bit set size 8

   char buff[32];
   datastream<char*> ds(buff, sizeof(buff));

   fcl::raw::pack(ds, bs1);

   fcl::dynamic_bitset bs2(8);
   ds.seekp(0);
   fcl::raw::unpack(ds, bs2);

   // 0b00011110
   BOOST_CHECK(!bs2.test(0));
   BOOST_CHECK(bs2.test(1));
   BOOST_CHECK(bs2.test(2));
   BOOST_CHECK(bs2.test(2));
   BOOST_CHECK(bs2.test(3));
   BOOST_CHECK(bs2.test(4));
   BOOST_CHECK(!bs2.test(5));
   BOOST_CHECK(!bs2.test(6));
   BOOST_CHECK(!bs2.test(7));
}

BOOST_AUTO_TEST_CASE(dynamic_bitset_large_test) {
   fcl::dynamic_bitset bs1;
   bs1.resize(12345);

   bs1.set(42);
   bs1.set(23);
   bs1.set(12000);

   auto packed = fcl::raw::pack(bs1);
   auto unpacked = fcl::raw::unpack<fcl::dynamic_bitset>(packed);

   BOOST_TEST(unpacked.at(42));
   BOOST_TEST(unpacked.at(23));
   BOOST_TEST(unpacked.at(12000));
   unpacked.flip(42);
   unpacked.flip(23);
   unpacked.flip(12000);
   BOOST_TEST(unpacked.none());
}

BOOST_AUTO_TEST_CASE(dynamic_bitset_small_test) {
   fcl::dynamic_bitset bs1;
   bs1.resize(21);

   bs1.set(2);
   bs1.set(7);

   auto packed = fcl::raw::pack(bs1);
   auto unpacked = fcl::raw::unpack<fcl::dynamic_bitset>(packed);

   BOOST_TEST(unpacked.at(2));
   BOOST_TEST(unpacked.at(7));
   unpacked.flip(2);
   unpacked.flip(7);
   BOOST_TEST(unpacked.none());
}

BOOST_AUTO_TEST_CASE(struct_serialization) {
   char buff[512];
   datastream<char*> ds(buff, sizeof(buff));

   A a{2, 2.2, "abc"};
   fcl::raw::pack(ds, a);

   A a2{0, 0};
   ds.seekp(0);
   fcl::raw::unpack(ds, a2);
   bool same = a == a2;
   BOOST_TEST(same);
}

// Verify std::optional is unpacked correctly, especially an empty optional will always
// be unpacked to an empty optional even the target is not empty
BOOST_AUTO_TEST_CASE(unpacking_optional) {
   // source is empty
   char buff[8];
   datastream<char*> ds(buff, sizeof(buff));
   std::optional<uint32_t> s; // no value
   fcl::raw::pack(ds, s);

   { // target has value. This test used to fail.
      std::optional<uint32_t> t = 10;
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target reused for multiple unpackings. This test used to fail.
      char buff[8];
      datastream<char*> ds1(buff, sizeof(buff));
      std::optional<uint32_t> s1 = 15;
      fcl::raw::pack(ds1, s1);

      std::optional<uint32_t> t; // target is empty initially

      // Unpacking to t the first time so t has value
      ds1.seekp(0);
      fcl::raw::unpack(ds1, t);
      BOOST_TEST((s1 == t));

      // Unpacking to t the second time. Afterwards, t does not have value.
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target is empty.
      std::optional<uint32_t> t;
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   // Source has value
   s = 5;
   ds.seekp(0);
   fcl::raw::pack(ds, s);

   { // target has value.
      std::optional<uint32_t> t = 10;
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target is empty.
      std::optional<uint32_t> t;
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }
}

// Verify std::shared_ptr is unpacked correctly, especially a null shared_ptr will always
// be unpacked to a null shared_ptr even if the target was not null.
BOOST_AUTO_TEST_CASE(packing_shared_ptr) {
   // source is null
   char buff[8];
   datastream<char*> ds(buff, sizeof(buff));
   std::shared_ptr<uint32_t> s; // null_ptr
   fcl::raw::pack(ds, s);

   { // target has value. This test used to fail.
      std::shared_ptr<uint32_t> t = std::make_shared<uint32_t>(10);
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST(!t);
   }

   { // target is null.
      std::shared_ptr<uint32_t> t;
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST(!t);
   }

   // source is not null
   ds.seekp(0);
   s = std::make_shared<uint32_t>(50);
   fcl::raw::pack(ds, s);

   { // target has value.
      std::shared_ptr<uint32_t> t = std::make_shared<uint32_t>(10);
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((*s == *t));
   }

   { // target is null.
      std::shared_ptr<uint32_t> t;
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((*s == *t));
   }
}

// Verify std::set is unpacked correctly, especially an empty set will always
// be unpacked to an empty set even if the target was not empty.
BOOST_AUTO_TEST_CASE(packing_set) {
   //==== source empty
   char buff[16];
   datastream<char*> ds(buff, sizeof(buff));
   std::set<uint32_t> s; // empty
   fcl::raw::pack(ds, s);

   { // target is not empty. This test used to fail.
      std::set<uint32_t> t{10};
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST(t.empty());
   }

   { // target is empty.
      std::set<uint32_t> t;
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST(t.empty());
   }

   // Source has values
   ds.seekp(0);
   s = {1, 2};
   fcl::raw::pack(ds, s);

   { // target is not empty. This test used to fail (ending up with {1, 2, 3}).
      std::set<uint32_t> t{3};
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target is empty.
      std::set<uint32_t> t;
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }
}

// Verify std::list is unpacked correctly, especially an empty list will always
// be unpacked to an empty list even if the target was not empty.
BOOST_AUTO_TEST_CASE(packing_list) {
   //==== source empty
   char buff[16];
   datastream<char*> ds(buff, sizeof(buff));
   std::list<uint32_t> s; // empty
   fcl::raw::pack(ds, s);

   { // target has value. This test used to fail.
      std::list<uint32_t> t{10};
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((t.size() == 0));
   }

   { // target is empty.
      std::list<uint32_t> t;
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((t.size() == 0));
   }

   // Source has values
   ds.seekp(0);
   s = {1, 2};
   fcl::raw::pack(ds, s);

   { // target is not empty. This test used to fail (ending up with {1, 2, 3}).
      std::list<uint32_t> t{3};
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target is empty.
      std::list<uint32_t> t;
      ds.seekp(0);
      fcl::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }
}

BOOST_AUTO_TEST_SUITE_END()
