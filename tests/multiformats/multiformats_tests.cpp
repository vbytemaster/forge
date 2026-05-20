#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <fcl/exception/macros.hpp>

import fcl.multiformats;
import fcl.exception.exception;

namespace {

[[nodiscard]] fcl::multiformats::bytes bytes_from_text(std::string_view text) {
   return {text.begin(), text.end()};
}

} // namespace

BOOST_AUTO_TEST_SUITE(multiformats)

BOOST_AUTO_TEST_CASE(varint_uses_minimal_unsigned_leb128_encoding) try {
   using fcl::multiformats::varint_decode;
   using fcl::multiformats::varint_encode;

   BOOST_CHECK(varint_encode(0).empty() == false);
   BOOST_CHECK_EQUAL(varint_encode(0).front(), 0);

   const auto encoded_300 = varint_encode(300);
   const std::vector<std::uint8_t> expected_300{0xac, 0x02};
   BOOST_CHECK_EQUAL_COLLECTIONS(encoded_300.begin(), encoded_300.end(), expected_300.begin(), expected_300.end());

   const auto decoded = varint_decode(expected_300);
   BOOST_CHECK_EQUAL(decoded.value, 300);
   BOOST_CHECK_EQUAL(decoded.size, 2);

   const std::vector<std::uint8_t> non_minimal{0x80, 0x00};
   BOOST_CHECK_THROW((void)varint_decode(non_minimal), fcl::multiformats::format_error);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(multicodec_constants_match_libp2p_foundation_codes) try {
   using enum fcl::multiformats::multicodec_code;

   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(identity), 0x00);
   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(sha2_256), 0x12);
   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(sha2_512), 0x13);
   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(libp2p_key), 0x72);
   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(ip4), 0x04);
   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(udp), 0x0111);
   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(quic_v1), 0x01cc);
   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(p2p), 0x01a5);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(multihash_roundtrips_identity_and_sha2_256) try {
   const auto hello = bytes_from_text("hello");

   auto identity = fcl::multiformats::multihash::identity(hello);
   BOOST_CHECK_EQUAL(identity.code, fcl::multiformats::code_value(fcl::multiformats::multicodec_code::identity));
   BOOST_CHECK_EQUAL_COLLECTIONS(identity.digest.begin(), identity.digest.end(), hello.begin(), hello.end());

   auto encoded_identity = identity.encode();
   auto decoded_identity = fcl::multiformats::multihash::decode(encoded_identity);
   BOOST_CHECK_EQUAL_COLLECTIONS(
       decoded_identity.digest.begin(), decoded_identity.digest.end(), hello.begin(), hello.end());

   auto sha = fcl::multiformats::multihash::sha2_256(hello);
   BOOST_CHECK_EQUAL(sha.digest_hex(), "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(multibase_uses_libp2p_required_prefixes) try {
   const auto hello = bytes_from_text("hello world");

   BOOST_CHECK_EQUAL(fcl::multiformats::multibase_encode(fcl::multiformats::multibase_code::base58btc, hello),
                     "zStV1DL6CwTryKyV");
   BOOST_CHECK_EQUAL(fcl::multiformats::multibase_encode(fcl::multiformats::multibase_code::base32, hello),
                     "bnbswy3dpeb3w64tmmq");

   auto decoded = fcl::multiformats::multibase_decode("BNBSWY3DPEB3W64TMMQ");
   BOOST_CHECK_EQUAL_COLLECTIONS(decoded.bytes.begin(), decoded.bytes.end(), hello.begin(), hello.end());
   BOOST_TEST(static_cast<int>(decoded.code) ==
              static_cast<int>(fcl::multiformats::multibase_code::base32_upper));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(address_parses_and_formats_libp2p_quic_endpoint) try {
   static_assert(std::is_same_v<decltype(fcl::multiformats::address{}.components()),
                                const std::vector<fcl::multiformats::address::component>&>);

   const auto value = std::string{"/ip4/127.0.0.1/udp/4001/quic-v1/p2p/12D3KooWJwq7uD4Kz3fJF7uY2QZtxJ3nSqJtV9f1UB1dJ5kF2Fq9"};
   auto address = fcl::multiformats::address::parse(value);
   BOOST_CHECK_EQUAL(address.to_string(), value);

   auto encoded = address.to_bytes();
   auto decoded = fcl::multiformats::address::from_bytes(encoded);
   BOOST_CHECK_EQUAL(decoded.to_string(), value);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
