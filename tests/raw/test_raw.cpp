#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/raw/serialization.hpp>
#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <deque>
#include <flat_map>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import forge.exceptions;
import forge.codec.hex;
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.exceptions;
import forge.raw.raw;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;
import forge.variant.dynamic_bitset;

using namespace forge;

struct A {
   int x;
   float y;
   std::optional<std::string> z;

   bool operator==(const A&) const = default;
};
BOOST_DESCRIBE_STRUCT(A, (), (x, y, z))

class short_write_streambuf final : public std::streambuf {
 protected:
   std::streamsize xsputn(const char*, std::streamsize size) override {
      return size > 0 ? size - 1 : 0;
   }
};

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

FORGE_DECLARE_SERIALIZATION(macro_serialized_record)
FORGE_IMPLEMENT_SERIALIZATION(macro_serialized_record)

struct stream_serialized_aggregate {
   std::uint32_t value = 0;

   bool operator==(const stream_serialized_aggregate&) const = default;

   template <typename Stream>
      requires requires(Stream& target, const char* data) { target.write(data, std::size_t{}); }
   friend Stream& operator<<(Stream& stream, const stream_serialized_aggregate& item) {
      forge::raw::pack(stream, std::uint8_t{0xa5});
      forge::raw::pack(stream, item.value);
      return stream;
   }

   template <typename Stream>
      requires requires(Stream& target, char* data) { target.read(data, std::size_t{}); }
   friend Stream& operator>>(Stream& stream, stream_serialized_aggregate& item) {
      auto marker = std::uint8_t{};
      forge::raw::unpack(stream, marker);
      forge::raw::detail::require(marker == 0xa5, "custom aggregate marker is invalid");
      forge::raw::unpack(stream, item.value);
      return stream;
   }
};

static_assert(std::is_aggregate_v<stream_serialized_aggregate>);

struct datastream_serialized_aggregate {
   std::uint32_t value = 0;

   bool operator==(const datastream_serialized_aggregate&) const = default;
};

template <typename Storage>
forge::datastream<Storage>& operator<<(forge::datastream<Storage>& stream,
                                       const datastream_serialized_aggregate& item) {
   forge::raw::pack(stream, std::uint8_t{0x5a});
   forge::raw::pack(stream, item.value);
   return stream;
}

template <typename Storage>
forge::datastream<Storage>& operator>>(forge::datastream<Storage>& stream, datastream_serialized_aggregate& item) {
   auto marker = std::uint8_t{};
   forge::raw::unpack(stream, marker);
   forge::raw::detail::require(marker == 0x5a, "datastream aggregate marker is invalid");
   forge::raw::unpack(stream, item.value);
   return stream;
}

static_assert(std::is_aggregate_v<datastream_serialized_aggregate>);

struct concrete_datastream_aggregate {
   std::uint32_t value = 0;
};

forge::datastream<std::vector<std::uint8_t>>& operator<<(forge::datastream<std::vector<std::uint8_t>>& stream,
                                                         const concrete_datastream_aggregate& item) {
   forge::raw::pack(stream, std::uint8_t{0xc3});
   forge::raw::pack(stream, item.value);
   return stream;
}

forge::datastream<std::vector<std::uint8_t>>& operator>>(forge::datastream<std::vector<std::uint8_t>>& stream,
                                                         concrete_datastream_aggregate& item) {
   auto marker = std::uint8_t{};
   forge::raw::unpack(stream, marker);
   forge::raw::detail::require(marker == 0xc3, "concrete datastream aggregate marker is invalid");
   forge::raw::unpack(stream, item.value);
   return stream;
}

static_assert(std::is_aggregate_v<concrete_datastream_aggregate>);

struct pointer_datastream_aggregate {
   std::uint32_t value = 0;
};

forge::datastream<std::uint8_t*>& operator<<(forge::datastream<std::uint8_t*>& stream,
                                             const pointer_datastream_aggregate& item) {
   forge::raw::pack(stream, std::uint8_t{0xd4});
   forge::raw::pack(stream, item.value);
   return stream;
}

forge::datastream<const std::uint8_t*>& operator>>(forge::datastream<const std::uint8_t*>& stream,
                                                   pointer_datastream_aggregate& item) {
   auto marker = std::uint8_t{};
   forge::raw::unpack(stream, marker);
   forge::raw::detail::require(marker == 0xd4, "pointer datastream aggregate marker is invalid");
   forge::raw::unpack(stream, item.value);
   return stream;
}

BOOST_AUTO_TEST_SUITE(raw_test_suite)

BOOST_AUTO_TEST_CASE(raw_string_golden_bytes) {
   const auto packed = forge::raw::pack(std::string("abc"));
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(packed), "03616263");
}

BOOST_AUTO_TEST_CASE(boost_describe_struct_preserves_fc_reflect_member_order) {
   const A value{2, 2.25f, std::string("abc")};
   const auto packed = forge::raw::pack(value);

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(packed), "02000000000010400103616263");

   const auto unpacked = forge::raw::unpack<A>(packed);
   BOOST_CHECK(value == unpacked);
}

BOOST_AUTO_TEST_CASE(boost_describe_enum_uses_old_reflected_enum_int64_layout) {
   const auto packed = forge::raw::pack(described_mode::write);

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(packed), "0900000000000000");

   const auto unpacked = forge::raw::unpack<described_mode>(packed);
   BOOST_CHECK(unpacked == described_mode::write);
}

BOOST_AUTO_TEST_CASE(boost_describe_derived_types_pack_base_first_then_local_members) {
   described_child value;
   value.parent = 0x1234;
   value.child = 0x56;

   const auto packed = forge::raw::pack(value);

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(packed), "341256");

   const auto unpacked = forge::raw::unpack<described_child>(packed);
   BOOST_CHECK(value == unpacked);
}

BOOST_AUTO_TEST_CASE(serialization_macros_instantiate_raw_variant_and_digest_pack_paths) {
   const macro_serialized_record value{0x1234, "node"};

   forge::variant variant_value;
   forge::to_variant(value, variant_value);
   auto from_variant = macro_serialized_record{};
   forge::from_variant(variant_value, from_variant);
   BOOST_CHECK(value == from_variant);

   forge::datastream<size_t> size_stream;
   forge::raw::pack(size_stream, value);
   BOOST_CHECK_EQUAL(size_stream.tellp(), 7u);

   std::uint8_t buffer[32]{};
   forge::datastream<std::uint8_t*> write_stream(buffer, sizeof(buffer));
   forge::raw::pack(write_stream, value);
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(std::span<const std::uint8_t>{buffer, write_stream.tellp()}),
                     "3412046e6f6465");

   forge::datastream<const std::uint8_t*> read_stream(buffer, write_stream.tellp());
   auto unpacked = macro_serialized_record{};
   forge::raw::unpack(read_stream, unpacked);
   BOOST_CHECK(value == unpacked);

   forge::crypto::digest::sha256::encoder digest_stream;
   forge::raw::pack(digest_stream, value);
   BOOST_CHECK_EQUAL(
       digest_stream.result().str(),
       forge::crypto::digest::sha256::hash(std::span<const std::uint8_t>{buffer, write_stream.tellp()}).str());
}

BOOST_AUTO_TEST_CASE(raw_pack_uses_canonical_uint8_byte_container) {
   const macro_serialized_record value{0x1234, "node"};

   const auto packed = forge::raw::pack(value);
   static_assert(std::same_as<std::remove_cvref_t<decltype(packed)>, forge::raw::bytes>);
   auto bytes = std::vector<std::uint8_t>{};
   forge::raw::pack(bytes, value);

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(bytes), forge::codec::hex::encode(packed));
   BOOST_CHECK(forge::raw::unpack<macro_serialized_record>(bytes) == value);

   const auto view = std::span<const std::uint8_t>{bytes.data(), bytes.size()};
   BOOST_CHECK(forge::raw::unpack<macro_serialized_record>(view) == value);
}

BOOST_AUTO_TEST_CASE(raw_unpack_limits_reject_container_allocation_before_resize) {
   const auto payload = forge::raw::bytes{0x03U};
   auto values = std::vector<std::uint32_t>{};

   BOOST_CHECK_THROW(forge::raw::unpack_exact(std::span<const std::uint8_t>{payload}, values,
                                              forge::raw::unpack_limits{.max_container_elements = 2U}),
                     forge::raw::exceptions::allocation_limit);
   BOOST_CHECK(values.empty());
}

BOOST_AUTO_TEST_CASE(raw_unpack_limits_reject_byte_allocation_before_resize) {
   const auto payload = forge::raw::bytes{0x40U};
   auto values = forge::raw::bytes{};

   BOOST_CHECK_THROW(forge::raw::unpack_exact(std::span<const std::uint8_t>{payload}, values,
                                              forge::raw::unpack_limits{.max_bytes = 8U}),
                     forge::raw::exceptions::allocation_limit);
   BOOST_CHECK(values.empty());
}

BOOST_AUTO_TEST_CASE(raw_unpack_limits_apply_the_root_container_budget_once) {
   const auto payload = forge::raw::pack(std::vector<std::vector<std::uint32_t>>{{1U}, {2U}});
   auto values = std::vector<std::vector<std::uint32_t>>{};

   BOOST_CHECK_THROW(forge::raw::unpack_exact(
                         std::span<const std::uint8_t>{payload}, values,
                         forge::raw::unpack_limits{.max_container_elements = 8U, .first_container_elements = 1U}),
                     forge::raw::exceptions::allocation_limit);
   BOOST_CHECK(values.empty());
}

BOOST_AUTO_TEST_CASE(raw_unpack_limits_apply_a_cumulative_container_budget) {
   const auto payload = forge::raw::pack(std::vector<std::vector<std::uint32_t>>{{1U}, {2U}});
   auto values = std::vector<std::vector<std::uint32_t>>{};

   BOOST_CHECK_THROW(forge::raw::unpack_exact(std::span<const std::uint8_t>{payload}, values,
                                              forge::raw::unpack_limits{.max_container_elements = 8U,
                                                                        .max_total_container_elements = 3U,
                                                                        .first_container_elements = 2U}),
                     forge::raw::exceptions::allocation_limit);
}

BOOST_AUTO_TEST_CASE(custom_stream_codec_precedes_aggregate_fallback) {
   const auto value = stream_serialized_aggregate{.value = 0x12345678};
   const auto packed = forge::raw::pack(value);

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(packed), "a578563412");
   BOOST_CHECK(forge::raw::unpack<stream_serialized_aggregate>(packed) == value);

   auto stream = forge::datastream<std::vector<std::uint8_t>>{};
   stream << value;
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(stream.storage()), "a578563412");

   stream.seekp(0);
   auto decoded = stream_serialized_aggregate{};
   stream >> decoded;
   BOOST_CHECK(decoded == value);
}

BOOST_AUTO_TEST_CASE(datastream_specific_codec_precedes_aggregate_fallback) {
   const auto value = datastream_serialized_aggregate{.value = 0x12345678};
   const auto packed = forge::raw::pack(value);

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(packed), "5a78563412");
   BOOST_CHECK(forge::raw::unpack<datastream_serialized_aggregate>(packed) == value);

   auto stream = forge::datastream<std::vector<std::uint8_t>>{};
   stream << value;
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(stream.storage()), "5a78563412");

   stream.seekp(0);
   auto decoded = datastream_serialized_aggregate{};
   stream >> decoded;
   BOOST_CHECK(decoded == value);
}

BOOST_AUTO_TEST_CASE(concrete_datastream_codec_precedes_aggregate_fallback) {
   const auto value = concrete_datastream_aggregate{.value = 0x12345678};
   auto stream = forge::datastream<std::vector<std::uint8_t>>{};

   stream << value;

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(stream.storage()), "c378563412");
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(value)), "c378563412");
   BOOST_CHECK_EQUAL(forge::raw::pack_size(value), 5U);
   BOOST_CHECK_EQUAL(forge::raw::unpack<concrete_datastream_aggregate>(forge::raw::pack(value)).value, value.value);
}

BOOST_AUTO_TEST_CASE(one_shot_pack_uses_one_stream_for_nested_aggregates) {
   const auto value = pointer_datastream_aggregate{.value = 0x12345678};
   auto bytes = std::array<std::uint8_t, 5>{};
   auto pointer_stream = forge::datastream<std::uint8_t*>{bytes.data(), bytes.size()};

   pointer_stream << value;

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(bytes), "d478563412");
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(value)), "78563412");
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(std::vector{value})), "0178563412");
   BOOST_CHECK_EQUAL(forge::raw::unpack<pointer_datastream_aggregate>(forge::raw::pack(value)).value, value.value);

   auto pointer_reader = forge::datastream<const std::uint8_t*>{bytes.data(), bytes.size()};
   auto pointer_decoded = pointer_datastream_aggregate{};
   pointer_reader >> pointer_decoded;
   BOOST_CHECK_EQUAL(pointer_decoded.value, value.value);
}

BOOST_AUTO_TEST_CASE(uint8_vector_datastream_reads_varint_prefixed_values) {
   auto stream = forge::datastream<std::vector<std::uint8_t>>{std::vector<std::uint8_t>{
       0x03, static_cast<std::uint8_t>('r'), static_cast<std::uint8_t>('a'), static_cast<std::uint8_t>('w')}};
   auto value = std::string{};

   forge::raw::unpack(stream, value);

   BOOST_CHECK_EQUAL(value, "raw");
   BOOST_CHECK_EQUAL(stream.remaining(), 0U);
}

BOOST_AUTO_TEST_CASE(deque_and_streambuf_datastreams_read_varint_prefixed_values) {
   auto uint8_stream = forge::datastream<std::deque<std::uint8_t>>{std::deque<std::uint8_t>{
       0x03, static_cast<std::uint8_t>('r'), static_cast<std::uint8_t>('a'), static_cast<std::uint8_t>('w')}};
   auto uint8_value = std::string{};
   forge::raw::unpack(uint8_stream, uint8_value);
   BOOST_CHECK_EQUAL(uint8_value, "raw");
   BOOST_CHECK_EQUAL(uint8_stream.remaining(), 0U);

   auto char_stream =
       forge::datastream<std::deque<char>>{std::deque<char>{char{0x03}, char{'r'}, char{'a'}, char{'w'}}};
   auto char_value = std::string{};
   forge::raw::unpack(char_stream, char_value);
   BOOST_CHECK_EQUAL(char_value, "raw");
   BOOST_CHECK_EQUAL(char_stream.remaining(), 0U);

   auto streambuf = forge::datastream<std::stringbuf>{std::string{"\x03raw", 4}, std::ios_base::in};
   auto streambuf_value = std::string{};
   forge::raw::unpack(streambuf, streambuf_value);
   BOOST_CHECK_EQUAL(streambuf_value, "raw");
   BOOST_CHECK_EQUAL(streambuf.remaining(), 0U);
}

BOOST_AUTO_TEST_CASE(deque_and_streambuf_datastreams_read_signed_varints) {
   auto uint8_stream = forge::datastream<std::deque<std::uint8_t>>{std::deque<std::uint8_t>{0x81, 0x01}};
   auto uint8_value = forge::signed_int{};
   forge::raw::unpack(uint8_stream, uint8_value);
   BOOST_CHECK_EQUAL(uint8_value.value, -65);
   BOOST_CHECK_EQUAL(uint8_stream.remaining(), 0U);

   auto char_stream = forge::datastream<std::deque<char>>{std::deque<char>{static_cast<char>(0x81), char{0x01}}};
   auto char_value = forge::signed_int{};
   forge::raw::unpack(char_stream, char_value);
   BOOST_CHECK_EQUAL(char_value.value, -65);
   BOOST_CHECK_EQUAL(char_stream.remaining(), 0U);

   auto streambuf = forge::datastream<std::stringbuf>{std::string{"\x81\x01", 2}, std::ios_base::in};
   auto streambuf_value = forge::signed_int{};
   forge::raw::unpack(streambuf, streambuf_value);
   BOOST_CHECK_EQUAL(streambuf_value.value, -65);
   BOOST_CHECK_EQUAL(streambuf.remaining(), 0U);
}

BOOST_AUTO_TEST_CASE(signed_varint_rejects_uint32_overflow) {
   auto stream = forge::datastream<std::stringbuf>{std::string{"\xff\xff\xff\xff\x10", 5}, std::ios_base::in};
   auto value = forge::signed_int{};
   BOOST_CHECK_EXCEPTION(forge::raw::unpack(stream, value), forge::raw::exceptions::codec_error,
                         [](const forge::raw::exceptions::codec_error& error) {
                            return error.message() == "raw signed varint overflows int32";
                         });
}

BOOST_AUTO_TEST_CASE(streambuf_datastream_rejects_truncated_reads) {
   auto truncated_value = forge::datastream<std::stringbuf>{std::string{"\x03ra", 3}, std::ios_base::in};
   auto value = std::string{};
   BOOST_CHECK_THROW(forge::raw::unpack(truncated_value, value), forge::raw::exceptions::range_error);

   auto truncated_varint = forge::datastream<std::stringbuf>{std::string{"\x80", 1}, std::ios_base::in};
   auto varint = forge::unsigned_int{};
   BOOST_CHECK_THROW(forge::raw::unpack(truncated_varint, varint), forge::raw::exceptions::range_error);

   auto truncated_write = forge::datastream<short_write_streambuf>{};
   BOOST_CHECK_THROW(truncated_write.write("raw", 3U), forge::raw::exceptions::range_error);
}

BOOST_AUTO_TEST_CASE(char_and_uint8_values_preserve_spring_wire_bits) {
   const auto chars = std::vector<char>{char{0x00}, char{0x7f}, static_cast<char>(0x80), static_cast<char>(0xff)};
   const auto octets = std::vector<std::uint8_t>{0x00, 0x7f, 0x80, 0xff};
   BOOST_CHECK(forge::raw::pack(chars) == forge::raw::pack(octets));
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(chars)), "04007f80ff");

   const auto char_wire = forge::raw::pack(static_cast<char>(0xff));
   const auto octet_wire = forge::raw::pack(std::uint8_t{0xff});
   BOOST_CHECK(char_wire == octet_wire);
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(char_wire), "ff");
   BOOST_CHECK_EQUAL(static_cast<unsigned char>(forge::raw::unpack<char>(char_wire)), 0xffU);
   BOOST_CHECK_EQUAL(forge::raw::unpack<std::uint8_t>(octet_wire), 0xffU);
}

BOOST_AUTO_TEST_CASE(int128_values_preserve_spring_wire_bits_for_all_streams) {
   const auto unsigned_value = (static_cast<unsigned __int128>(0x0102030405060708ULL) << 64U) |
                               static_cast<unsigned __int128>(0x1112131415161718ULL);
   const auto unsigned_wire = forge::raw::pack(unsigned_value);

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(unsigned_wire), "18171615141312110807060504030201");
   BOOST_CHECK(forge::raw::unpack<unsigned __int128>(unsigned_wire) == unsigned_value);

   const auto signed_value = static_cast<__int128>(-2);
   const auto signed_wire = forge::raw::pack(signed_value);
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(signed_wire), "feffffffffffffffffffffffffffffff");
   BOOST_CHECK(forge::raw::unpack<__int128>(signed_wire) == signed_value);

   auto digest_stream = forge::crypto::digest::sha256::encoder{};
   forge::raw::pack(digest_stream, unsigned_value);
   BOOST_CHECK_EQUAL(digest_stream.result().str(),
                     forge::crypto::digest::sha256::hash(std::span<const std::uint8_t>{unsigned_wire}).str());
}

BOOST_AUTO_TEST_CASE(std_array_pointer_elements_use_element_codec) {
   const auto values = std::array<const char*, 2>{"a", "bc"};
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(values)), "0161026263");
}

BOOST_AUTO_TEST_CASE(std_flat_map_preserves_spring_sorted_map_wire_layout) {
   auto value = std::flat_map<std::uint32_t, std::uint32_t>{};
   value.emplace(2U, 12U);
   value.emplace(1U, 11U);

   const auto packed = forge::raw::pack(value);
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(packed), "02010000000b000000020000000c000000");
   BOOST_CHECK((forge::raw::unpack<std::flat_map<std::uint32_t, std::uint32_t>>(packed) == value));
}

BOOST_AUTO_TEST_CASE(target_neutral_containers_preserve_spring_wire_layout) {
   const auto map = std::map<std::uint32_t, std::uint32_t>{{2U, 12U}, {1U, 11U}};
   const auto set = std::set<std::uint32_t>{1U, 2U};
   const auto deque = std::deque<std::uint32_t>{1U, 2U};
   const auto list = std::list<std::uint32_t>{1U, 2U};
   const auto expected_map = std::string{"02010000000b000000020000000c000000"};
   const auto expected_sequence = std::string{"020100000002000000"};

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(map)), expected_map);
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(set)), expected_sequence);
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(deque)), expected_sequence);
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(list)), expected_sequence);
   BOOST_CHECK((forge::raw::unpack<decltype(map)>(forge::raw::pack(map)) == map));
   BOOST_CHECK((forge::raw::unpack<decltype(set)>(forge::raw::pack(set)) == set));
   BOOST_CHECK((forge::raw::unpack<decltype(deque)>(forge::raw::pack(deque)) == deque));
   BOOST_CHECK((forge::raw::unpack<decltype(list)>(forge::raw::pack(list)) == list));
}

BOOST_AUTO_TEST_CASE(unknown_variant_wire_type_throws_codec_error) {
   const std::vector<std::uint8_t> invalid_variant{0xff};
   BOOST_CHECK_EXCEPTION((void)forge::raw::unpack<forge::variant>(invalid_variant), forge::raw::exceptions::codec_error,
                         [](const forge::raw::exceptions::codec_error& error) {
                            return error.code().category().name() == std::string_view{"forge.raw"};
                         });
}

BOOST_AUTO_TEST_CASE(std_chrono_preserves_old_fc_raw_layout) {
   using sys_time_us = std::chrono::sys_time<std::chrono::microseconds>;

   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(sys_time_us{})), "0000000000000000");
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(sys_time_us{std::chrono::seconds{1}})),
                     "40420f0000000000");
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(std::chrono::sys_seconds{std::chrono::seconds{1}})),
                     "01000000");
   BOOST_CHECK_EQUAL(forge::codec::hex::encode(forge::raw::pack(std::chrono::microseconds{-1})), "ffffffffffffffff");

   BOOST_CHECK(forge::raw::unpack<sys_time_us>(forge::raw::pack(sys_time_us{std::chrono::seconds{1}})) ==
               sys_time_us{std::chrono::seconds{1}});
   BOOST_CHECK(forge::raw::unpack<std::chrono::sys_seconds>(forge::raw::pack(std::chrono::sys_seconds{
                   std::chrono::seconds{1}})) == std::chrono::sys_seconds{std::chrono::seconds{1}});
   BOOST_CHECK(forge::raw::unpack<std::chrono::microseconds>(forge::raw::pack(std::chrono::microseconds{-1})) ==
               std::chrono::microseconds{-1});
   BOOST_CHECK_THROW(forge::raw::pack(std::chrono::sys_seconds{std::chrono::seconds{-1}}), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(dynamic_bitset_test) {
   constexpr uint8_t bits = 0b00011110;
   forge::dynamic_bitset bs1(8, bits); // bit set size 8

   char buff[32];
   datastream<char*> ds(buff, sizeof(buff));

   forge::raw::pack(ds, bs1);

   forge::dynamic_bitset bs2(8);
   ds.seekp(0);
   forge::raw::unpack(ds, bs2);

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
   forge::dynamic_bitset bs1;
   bs1.resize(12345);

   bs1.set(42);
   bs1.set(23);
   bs1.set(12000);

   auto packed = forge::raw::pack(bs1);
   auto unpacked = forge::raw::unpack<forge::dynamic_bitset>(packed);

   BOOST_TEST(unpacked.at(42));
   BOOST_TEST(unpacked.at(23));
   BOOST_TEST(unpacked.at(12000));
   unpacked.flip(42);
   unpacked.flip(23);
   unpacked.flip(12000);
   BOOST_TEST(unpacked.none());
}

BOOST_AUTO_TEST_CASE(dynamic_bitset_small_test) {
   forge::dynamic_bitset bs1;
   bs1.resize(21);

   bs1.set(2);
   bs1.set(7);

   auto packed = forge::raw::pack(bs1);
   auto unpacked = forge::raw::unpack<forge::dynamic_bitset>(packed);

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
   forge::raw::pack(ds, a);

   A a2{0, 0};
   ds.seekp(0);
   forge::raw::unpack(ds, a2);
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
   forge::raw::pack(ds, s);

   { // target has value. This test used to fail.
      std::optional<uint32_t> t = 10;
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target reused for multiple unpackings. This test used to fail.
      char buff[8];
      datastream<char*> ds1(buff, sizeof(buff));
      std::optional<uint32_t> s1 = 15;
      forge::raw::pack(ds1, s1);

      std::optional<uint32_t> t; // target is empty initially

      // Unpacking to t the first time so t has value
      ds1.seekp(0);
      forge::raw::unpack(ds1, t);
      BOOST_TEST((s1 == t));

      // Unpacking to t the second time. Afterwards, t does not have value.
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target is empty.
      std::optional<uint32_t> t;
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   // Source has value
   s = 5;
   ds.seekp(0);
   forge::raw::pack(ds, s);

   { // target has value.
      std::optional<uint32_t> t = 10;
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target is empty.
      std::optional<uint32_t> t;
      ds.seekp(0);
      forge::raw::unpack(ds, t);
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
   forge::raw::pack(ds, s);

   { // target has value. This test used to fail.
      std::shared_ptr<uint32_t> t = std::make_shared<uint32_t>(10);
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST(!t);
   }

   { // target is null.
      std::shared_ptr<uint32_t> t;
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST(!t);
   }

   // source is not null
   ds.seekp(0);
   s = std::make_shared<uint32_t>(50);
   forge::raw::pack(ds, s);

   { // target has value.
      std::shared_ptr<uint32_t> t = std::make_shared<uint32_t>(10);
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST((*s == *t));
   }

   { // target is null.
      std::shared_ptr<uint32_t> t;
      ds.seekp(0);
      forge::raw::unpack(ds, t);
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
   forge::raw::pack(ds, s);

   { // target is not empty. This test used to fail.
      std::set<uint32_t> t{10};
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST(t.empty());
   }

   { // target is empty.
      std::set<uint32_t> t;
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST(t.empty());
   }

   // Source has values
   ds.seekp(0);
   s = {1, 2};
   forge::raw::pack(ds, s);

   { // target is not empty. This test used to fail (ending up with {1, 2, 3}).
      std::set<uint32_t> t{3};
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target is empty.
      std::set<uint32_t> t;
      ds.seekp(0);
      forge::raw::unpack(ds, t);
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
   forge::raw::pack(ds, s);

   { // target has value. This test used to fail.
      std::list<uint32_t> t{10};
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST((t.size() == 0));
   }

   { // target is empty.
      std::list<uint32_t> t;
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST((t.size() == 0));
   }

   // Source has values
   ds.seekp(0);
   s = {1, 2};
   forge::raw::pack(ds, s);

   { // target is not empty. This test used to fail (ending up with {1, 2, 3}).
      std::list<uint32_t> t{3};
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target is empty.
      std::list<uint32_t> t;
      ds.seekp(0);
      forge::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }
}

BOOST_AUTO_TEST_SUITE_END()
