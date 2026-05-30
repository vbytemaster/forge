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
   BOOST_CHECK_THROW((void)varint_decode(non_minimal), fcl::multiformats::exceptions::invalid_format);
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
   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(quic), 0x01cc);
   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(quic_v1), 0x01cd);
   BOOST_CHECK_EQUAL(fcl::multiformats::code_value(p2p_circuit), 0x0122);
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

BOOST_AUTO_TEST_CASE(multiaddr_matches_donor_tcp_and_quic_binary_vectors) try {
   static_assert(std::is_same_v<decltype(fcl::multiformats::multiaddr{}.components()),
                                const std::vector<fcl::multiformats::multiaddr_component>&>);

   const auto tcp = fcl::multiformats::multiaddr::parse("/ip4/192.0.2.42/tcp/443");
   const auto expected_tcp = fcl::multiformats::bytes{0x04, 0xc0, 0x00, 0x02, 0x2a, 0x06, 0x01, 0xbb};
   const auto encoded_tcp = tcp.to_bytes();
   BOOST_CHECK_EQUAL(tcp.to_string(), "/ip4/192.0.2.42/tcp/443");
   BOOST_CHECK_EQUAL_COLLECTIONS(encoded_tcp.begin(), encoded_tcp.end(), expected_tcp.begin(), expected_tcp.end());
   BOOST_CHECK_EQUAL(fcl::multiformats::multiaddr::from_bytes(expected_tcp).to_string(), "/ip4/192.0.2.42/tcp/443");

   const auto quic = fcl::multiformats::multiaddr::parse("/ip4/127.0.0.1/udp/4001/quic-v1");
   const auto expected_quic =
       fcl::multiformats::bytes{0x04, 0x7f, 0x00, 0x00, 0x01, 0x91, 0x02, 0x0f, 0xa1, 0xcd, 0x03};
   const auto encoded_quic = quic.to_bytes();
   BOOST_CHECK_EQUAL(quic.to_string(), "/ip4/127.0.0.1/udp/4001/quic-v1");
   BOOST_CHECK_EQUAL_COLLECTIONS(encoded_quic.begin(), encoded_quic.end(), expected_quic.begin(), expected_quic.end());
   BOOST_CHECK_EQUAL(fcl::multiformats::multiaddr::from_bytes(expected_quic).to_string(),
                     "/ip4/127.0.0.1/udp/4001/quic-v1");
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(multiaddr_roundtrips_dns_wss_peer_and_relay_circuit) try {
   const auto peer = std::string{"QmcgpsyWgH8Y8ajJz1Cu72KnS5uo2Aa2LpzU7kinSupNKC"};
   const auto wss = "/dns4/example.com/tcp/443/wss/p2p/" + peer;
   const auto relayed = "/ip4/127.0.0.1/tcp/9090/p2p-circuit/p2p/" + peer;

   auto wss_address = fcl::multiformats::multiaddr::parse(wss);
   BOOST_CHECK_EQUAL(wss_address.to_string(), wss);
   BOOST_CHECK_EQUAL(fcl::multiformats::multiaddr::from_bytes(wss_address.to_bytes()).to_string(), wss);

   auto relayed_address = fcl::multiformats::multiaddr::parse(relayed);
   BOOST_CHECK_EQUAL(relayed_address.to_string(), relayed);
   BOOST_CHECK_EQUAL(fcl::multiformats::multiaddr::from_bytes(relayed_address.to_bytes()).to_string(), relayed);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(multiaddr_encapsulates_and_decapsulates_like_libp2p) try {
   const auto base = fcl::multiformats::multiaddr::parse("/ip4/1.2.3.4");
   const auto inner = fcl::multiformats::multiaddr::parse("/tcp/80/ws");
   BOOST_CHECK_EQUAL(base.encapsulate(inner).to_string(), "/ip4/1.2.3.4/tcp/80/ws");

   const auto quic = fcl::multiformats::multiaddr::parse("/ip4/1.2.3.6/udp/1234/quic-v1");
   BOOST_CHECK_EQUAL(quic.decapsulate(fcl::multiformats::multiaddr::parse("/udp/1234")).to_string(),
                     "/ip4/1.2.3.6");
   BOOST_CHECK_EQUAL(quic.decapsulate(fcl::multiformats::multiaddr::parse("/udp/1234/quic-v1")).to_string(),
                     "/ip4/1.2.3.6");
   BOOST_CHECK_EQUAL(quic.decapsulate(fcl::multiformats::multiaddr::parse("/tcp/80")).to_string(),
                     "/ip4/1.2.3.6/udp/1234/quic-v1");
   BOOST_CHECK_EQUAL(fcl::multiformats::multiaddr::parse("/ip4/1.2.3.4").decapsulate(base).to_string(), "");
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(multiaddr_rejects_malformed_donor_cases_with_typed_errors) try {
   BOOST_CHECK_THROW((void)fcl::multiformats::multiaddr::parse("/ip4/127.0.0.1/quic-v1/1234"),
                     fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW((void)fcl::multiformats::multiaddr::parse("/ip4/not-an-ip/tcp/80"),
                     fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW((void)fcl::multiformats::multiaddr::parse("/ip4/127.0.0.1/tcp/not-a-port"),
                     fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW((void)fcl::multiformats::multiaddr::parse("/ip4/127.0.0.1/tcp/65536"),
                     fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW((void)fcl::multiformats::multiaddr::parse("/ip4/127.0.0.1/p2p/not-base58-0"),
                     fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW((void)fcl::multiformats::multiaddr::from_bytes(fcl::multiformats::bytes{0x04, 0x7f}),
                     fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW((void)fcl::multiformats::multiaddr::from_bytes(fcl::multiformats::bytes{0xff, 0xff, 0xff, 0xff}),
                     fcl::multiformats::exceptions::invalid_format);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(multiaddr_push_rejects_invalid_component_state) try {
   using enum fcl::multiformats::protocol_code;

   auto value = fcl::multiformats::multiaddr{};
   BOOST_CHECK_THROW(value.push({.code = ip4, .value = {}}), fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW(value.push({.code = tcp, .value = {}}), fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW(value.push({.code = p2p, .value = {}}), fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW(value.push({.code = quic_v1, .value = "unexpected"}), fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW(value.push({.code = ws, .value = "unexpected"}), fcl::multiformats::exceptions::invalid_format);
   BOOST_CHECK_THROW(value.push({.code = wss, .value = "unexpected"}), fcl::multiformats::exceptions::invalid_format);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
