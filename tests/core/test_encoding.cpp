#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import fcl.core.encoding;

namespace {

[[nodiscard]] std::vector<std::uint8_t> bytes(std::string_view text) {
   return {text.begin(), text.end()};
}

[[nodiscard]] std::string text(std::span<const std::uint8_t> value) {
   return {value.begin(), value.end()};
}

} // namespace

BOOST_AUTO_TEST_SUITE(core_encoding_test_suite)

BOOST_AUTO_TEST_CASE(base64_roundtrips_padded_and_unpadded) {
   const auto one = bytes("a");
   BOOST_TEST(fcl::encoding::to_base64(one) == "YQ==");
   BOOST_TEST(text(fcl::encoding::from_base64("YQ==")) == "a");
   BOOST_TEST(text(fcl::encoding::from_base64("YQ")) == "a");

   const auto two = bytes("ab");
   BOOST_TEST(fcl::encoding::to_base64(two) == "YWI=");
   BOOST_TEST(text(fcl::encoding::from_base64("YWI=")) == "ab");
   BOOST_TEST(text(fcl::encoding::from_base64("YWI")) == "ab");

   const auto three = bytes("abc");
   BOOST_TEST(fcl::encoding::to_base64(three) == "YWJj");
   BOOST_TEST(text(fcl::encoding::from_base64("YWJj")) == "abc");
}

BOOST_AUTO_TEST_CASE(base64_rejects_malformed_padding_and_non_canonical_bits) {
   BOOST_CHECK_THROW(static_cast<void>(fcl::encoding::from_base64("YQ==evil")), std::invalid_argument);
   BOOST_CHECK_THROW(static_cast<void>(fcl::encoding::from_base64("Y=Q=")), std::invalid_argument);
   BOOST_CHECK_THROW(static_cast<void>(fcl::encoding::from_base64("YQ===")), std::invalid_argument);
   BOOST_CHECK_THROW(static_cast<void>(fcl::encoding::from_base64("Y")), std::invalid_argument);
   BOOST_CHECK_THROW(static_cast<void>(fcl::encoding::from_base64("AB==")), std::invalid_argument);
   BOOST_CHECK_THROW(static_cast<void>(fcl::encoding::from_base64("AAB=")), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(hex_rejects_odd_length_and_invalid_characters) {
   auto output = std::array<std::uint8_t, 2>{};
   BOOST_CHECK_THROW(static_cast<void>(fcl::encoding::from_hex("abc", output)), std::invalid_argument);
   BOOST_CHECK_THROW(static_cast<void>(fcl::encoding::from_hex("zz", output)), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
