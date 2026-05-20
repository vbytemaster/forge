#include <boost/test/unit_test.hpp>
#include <boost/describe.hpp>

#include <chrono>
#include <cstdint>
#include <future>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.api;
import fcl.p2p.api;
import fcl.p2p.codec;
import fcl.p2p.endpoint;
import fcl.p2p.errors;
import fcl.p2p.exceptions;
import fcl.p2p.identity;
import fcl.p2p.message;
import fcl.p2p.node;
import fcl.p2p.peer_store;
import fcl.p2p.protocol;
import fcl.p2p.relay;
import fcl.quic.endpoint;
import fcl.quic.framed_stream;
import fcl.quic.libp2p;
import fcl.quic.listener;
import fcl.quic.options;
import fcl.quic.security;
import fcl.quic.stream;
import fcl.multiformats;

namespace fcl::p2p {
namespace {

struct product_announce {
   std::string ref;

   bool operator==(const product_announce&) const = default;
};

BOOST_DESCRIBE_STRUCT(product_announce, (), (ref))

std::string_view test_certificate() {
   return "-----BEGIN CERTIFICATE-----\n"
          "MIICpDCCAYwCCQCJjaEDxrQqBzANBgkqhkiG9w0BAQsFADAUMRIwEAYDVQQDDAkx\n"
          "MjcuMC4wLjEwHhcNMjYwNDI5MDgwMTMzWhcNMjYwNDMwMDgwMTMzWjAUMRIwEAYD\n"
          "VQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDy\n"
          "sbPH/R4QUz725sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TX\n"
          "gl9tHkNpKmI92s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+\n"
          "x7MRWXfKYd/ArGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOS\n"
          "lI/lDqIjZxo7jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuv\n"
          "M+mTj6eO4UQ42w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXj\n"
          "nPOZzBinLRTDnE59HbDZAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAHSOUQTEDgjC\n"
          "uwza9ayfThJTs43j+TziWHLlowqCiHt/ipRNFEW7L0ibTnbMdQBFGfaLkTAhc5Rd\n"
          "6O6x+9o76pgEYxEg0rDkgNXmprNmS+nL7Are+iiF6R+X8dts3MQgtONPApAXE96P\n"
          "/n5K4GDQTd3WCI37hkmJA6rmwziFDTlwqtKWts39g8PqAbXac27rVR/iD0gWdOws\n"
          "qiaoGj/0WW9qcgjYGdCc0/CbbnyiWbi48VVf0yyfm7wgcz90byaKIQchHdb/qjyU\n"
          "wy7nfU5TJ5MKQ5yeqPTWmPYZZp9TKa5VD6wZD/IH7jH3GdJ/fSyroVLZktVnmxJa\n"
          "dmG/9wwivwQ=\n"
          "-----END CERTIFICATE-----\n";
}

std::string_view test_private_key() {
   return "-----BEGIN PRIVATE KEY-----\n"
          "MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQDysbPH/R4QUz72\n"
          "5sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TXgl9tHkNpKmI9\n"
          "2s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+x7MRWXfKYd/A\n"
          "rGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOSlI/lDqIjZxo7\n"
          "jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuvM+mTj6eO4UQ4\n"
          "2w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXjnPOZzBinLRTD\n"
          "nE59HbDZAgMBAAECggEBAIWVjHhy+V5RA+JRCh/12ayirNLG2BF30OP9pf7iL4IT\n"
          "/dMPbKvkmDGLw+1bW8tgKXj5+N6N/trfCm4zhqI3OF7ihooH9qYM88/F/OvMjFiU\n"
          "BhMVVhJW1LxtPPjKUcFN58M8VnMhRM9v6gIaoSOJZvpU1abVtgBDocyJUxAB6gYp\n"
          "i7MzoRwHGsL5mW/luE5H92/S8NNwLWBDA7DIGfrTZ6POf92h5I5W3CuTcqR5FICz\n"
          "3pfU3i443yZmsmkc9duH2gZ9cb9j4pRtNLbbsGmRVrBlgnkVFk8JWbikc8MpLeKO\n"
          "VKP7A2NvxJIrc7oFYrf4hbw8P70YL7S9B3W3yBPPzJECgYEA+Y3nG8CtvVTE/Keo\n"
          "qb5Rljlnj9DEffrylLyYUYfSSNR4Olc2WCPBiz0rPCDdO0VGeXAwqLf2VP7IEyAx\n"
          "kvrnqhzHWMhiLv+k4tIVyKCwpuofN0JsoUCi7CwRf+H2Pg+t6ewLV116THKsd41H\n"
          "IRElWyEvZsmbbhlLrsxUtfFZWnUCgYEA+PZwXUn+cb8kRmfG959gMawTtcfvnBUX\n"
          "sIn7LQl/ZWUIiLMWCaS3FbqkiGjaEYo6om1invYNJNA9zp/ECauSDp58NICCL0ie\n"
          "L7z26sEa6Ocg2VdR4ezpN3cM6dyAKfTFGb9V6qjyqNIPCE4eey6ZJ+CU/mpEfSDu\n"
          "+RGMzfdDCFUCgYEA5FRUn0zk6jU0YyMXq+9pgLSXL7vI/Kdt6m7AQuCto1tbga2o\n"
          "GG7mt/pIo6RCJufUemoO62AeL1hKQU2UbjHJYxkfv/jf9LaM68dijQWRe7b8xres\n"
          "4sFcEBCmFkbt4YzBCCWjntT1gBrv+Ba4fOXOMxoi374Yy1yzpYRpAWuI4L0CgYAn\n"
          "u1SlXrivuHx2i/tR62pzou2mVhkkRK16LBsczeY57UzWXBZJRbM+UYIOjwU2RWQk\n"
          "JebWTZg9ZspmXlLv5CS0FpDl5BhiqWktXy/cuSKtRq2UYf4cWy3A/0vdSqZdi8Wk\n"
          "3Uc94uaPEK77eVQd/orMtWexzo3NlmLs9uMMv8g/3QKBgQCbik0UoJkkqNRMmWG8\n"
          "dKQzj58eRI8fmKdJlWNfj2QMspd2vXMbsWYgAbFbU1QcVs1n8PxNydM+cfy77w8q\n"
          "NWMlYP7rUFQ3ekYWqrRlshZdJ/h24PALd1nPCvhc4C9dvn+zW3BLVez1lBuFO8n8\n"
          "0YkgmTgW7Ieibqnf4DqYp//nkw==\n"
          "-----END PRIVATE KEY-----\n";
}

peer_id peer(std::uint8_t value) {
   const auto payload = fcl::multiformats::bytes{value};
   return peer_id::from_bytes(fcl::multiformats::multihash::identity(payload).encode());
}

std::uint8_t hex_value(char value) {
   if (value >= '0' && value <= '9') {
      return static_cast<std::uint8_t>(value - '0');
   }
   if (value >= 'a' && value <= 'f') {
      return static_cast<std::uint8_t>(10 + value - 'a');
   }
   if (value >= 'A' && value <= 'F') {
      return static_cast<std::uint8_t>(10 + value - 'A');
   }
   throw std::runtime_error{"bad hex"};
}

std::vector<std::uint8_t> bytes_from_hex(std::string_view hex) {
   if ((hex.size() % 2) != 0) {
      throw std::runtime_error{"odd hex"};
   }
   auto out = std::vector<std::uint8_t>{};
   out.reserve(hex.size() / 2);
   for (std::size_t i = 0; i < hex.size(); i += 2) {
      out.push_back(static_cast<std::uint8_t>((hex_value(hex[i]) << 4U) | hex_value(hex[i + 1])));
   }
   return out;
}

node::options options_for(peer_id id, capability_set capabilities = capability_set{
                                           .bits = capabilities::direct_quic | capabilities::peer_exchange}) {
   return node::options{
       .certificate_pem = std::string{test_certificate()},
       .private_key_pem = std::string{test_private_key()},
       .explicit_peer_id = std::move(id),
       .capabilities = capabilities,
       .allow_insecure_test_mode = true,
   };
}

fcl::quic::server_options raw_quic_server_options() {
   return fcl::quic::server_options{
       .alpn = "fcl-p2p/1",
       .handshake_timeout = std::chrono::milliseconds{2'000},
       .idle_timeout = std::chrono::milliseconds{2'000},
       .security = fcl::quic::security_options{.verify_peer = false},
       .certificate_pem = std::string{test_certificate()},
       .private_key_pem = std::string{test_private_key()},
   };
}

void register_echo(node& value) {
   value.register_protocol_handler(builtins::echo,
                                   [](node::incoming_protocol_stream incoming) mutable -> boost::asio::awaitable<void> {
                                      auto payload = co_await incoming.stream.async_read_frame();
                                      co_await incoming.stream.async_write_frame(payload);
                                   });
}

fcl::quic::endpoint listen(node& value, fcl::asio::runtime& runtime) {
   fcl::asio::blocking::run(runtime, value.async_listen(fcl::quic::endpoint{.host = "127.0.0.1", .port = 0}));
   auto endpoint = value.local_endpoint();
   BOOST_REQUIRE(endpoint.has_value());
   return *endpoint;
}

void wait_for_server(std::future<void>& future, std::chrono::milliseconds timeout, std::string_view label) {
   if (future.wait_for(timeout) != std::future_status::ready) {
      throw std::runtime_error{std::string{label} + " did not finish"};
   }
   future.get();
}

boost::asio::awaitable<void> wait_before_close(fcl::quic::connection& connection, std::chrono::milliseconds delay) {
   auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
   timer.expires_after(delay);
   boost::system::error_code ec;
   co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
   try {
      co_await connection.async_close();
   } catch (...) {
      connection.cancel();
   }
}

void wait_on_runtime(fcl::asio::runtime& runtime, std::chrono::milliseconds delay, std::string_view label) {
   auto future = boost::asio::co_spawn(
       runtime.context(),
       [delay]() -> boost::asio::awaitable<void> {
          auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
          timer.expires_after(delay);
          boost::system::error_code ec;
          co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
       },
       boost::asio::use_future);
   wait_for_server(future, delay + std::chrono::milliseconds{1'000}, label);
}

boost::asio::awaitable<fcl::quic::connection> accept_raw_p2p_session(fcl::quic::listener& listener,
                                                                     peer_id local_peer) {
   auto connection = co_await listener.async_accept();
   auto control = fcl::quic::framed_stream{
       co_await connection.async_accept_stream(),
   };
   auto request = co_await control_codec::async_read(control);
   if (request.kind != control_message::type::hello) {
      throw std::runtime_error{"raw P2P test peer expected hello"};
   }
   co_await control_codec::async_write(
       control, control_message{
                    .kind = control_message::type::hello_ack,
                    .peer = std::move(local_peer),
                    .capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange},
                });
   co_return connection;
}

} // namespace

BOOST_AUTO_TEST_CASE(p2p_identity_uses_libp2p_multihash_shape) {
   const auto id = make_peer_id_from_certificate_pem(test_certificate());

   BOOST_TEST(valid_peer_id(id));
   auto decoded = fcl::multiformats::multihash::decode(id.to_bytes());
   BOOST_TEST(decoded.code == fcl::multiformats::code_value(fcl::multiformats::multicodec_code::sha2_256));
   BOOST_TEST(decoded.digest.size() == 32U);
}

BOOST_AUTO_TEST_CASE(p2p_public_key_encoding_matches_libp2p_vectors) {
   const auto ed25519_public_key =
       bytes_from_hex("1ed1e8fae2c4a144b8be8fd4b47bf3d3b34b871c3cacf6010f0e42d474fce27e");
   const auto ed25519_encoded = encode_public_key({.type = public_key::type::ed25519, .data = ed25519_public_key});
   BOOST_CHECK_EQUAL(
       fcl::multiformats::multihash::identity(ed25519_encoded).digest_hex(),
       "080112201ed1e8fae2c4a144b8be8fd4b47bf3d3b34b871c3cacf6010f0e42d474fce27e");

   auto ed25519_peer = make_peer_id({.type = public_key::type::ed25519, .data = ed25519_public_key});
   auto ed25519_hash = fcl::multiformats::multihash::decode(ed25519_peer.to_bytes());
   BOOST_TEST(ed25519_hash.code == fcl::multiformats::code_value(fcl::multiformats::multicodec_code::identity));

   const auto ecdsa_public_key = bytes_from_hex(
       "3059301306072a8648ce3d020106082a8648ce3d03010703420004de3d300fa36ae0e8f5d530899d83abab44ab"
       "f3161f162a4bc901d8e6ecda020e8b6d5f8da30525e71d6851510c098e5c47c646a597fb4dcec034e9f77c409e62");
   auto ecdsa_peer = make_peer_id({.type = public_key::type::ecdsa, .data = ecdsa_public_key});
   auto ecdsa_hash = fcl::multiformats::multihash::decode(ecdsa_peer.to_bytes());
   BOOST_TEST(ecdsa_hash.code == fcl::multiformats::code_value(fcl::multiformats::multicodec_code::sha2_256));
}

BOOST_AUTO_TEST_CASE(p2p_peer_id_legacy_and_cid_strings_roundtrip) {
   auto id = make_peer_id(
       {.type = public_key::type::secp256k1,
        .data = bytes_from_hex("037777e994e452c21604f91de093ce415f5432f701dd8cd1a7a6fea0e630bfca99")});

   auto legacy = id.to_string();
   BOOST_TEST(peer_id::from_string(legacy).to_string() == id.to_string());

   auto cid = id.to_cid_string();
   BOOST_TEST(cid.front() == 'b');
   BOOST_TEST(peer_id::from_string(cid).to_string() == id.to_string());
}

BOOST_AUTO_TEST_CASE(p2p_endpoint_parses_libp2p_quic_address_format) {
   static_assert(std::is_same_v<decltype(endpoint{}.kind), endpoint::address_kind>);

   const auto id = peer(42);
   auto parsed = parse_endpoint("/ip4/127.0.0.1/udp/4001/quic-v1/p2p/" + id.to_string());

   BOOST_TEST(static_cast<int>(parsed.kind) == static_cast<int>(endpoint::address_kind::ip4));

   BOOST_TEST(parsed.host == "127.0.0.1");
   BOOST_TEST(parsed.port == 4001);
   BOOST_REQUIRE(parsed.peer.has_value());
   BOOST_TEST(parsed.peer->to_string() == id.to_string());
   BOOST_TEST(parsed.to_string() == "/ip4/127.0.0.1/udp/4001/quic-v1/p2p/" + id.to_string());
   BOOST_TEST(parsed.quic_endpoint().authority() == "127.0.0.1:4001");
}

BOOST_AUTO_TEST_CASE(quic_libp2p_profile_sets_required_alpn) {
   auto client = fcl::quic::libp2p::client_profile();
   auto server = fcl::quic::libp2p::server_profile();

   BOOST_TEST(client.alpn == "libp2p");
   BOOST_TEST(server.alpn == "libp2p");
   BOOST_TEST(fcl::quic::libp2p::is_profile_alpn(client.alpn));
}

BOOST_AUTO_TEST_CASE(p2p_api_binding_enforces_known_peer_policy_before_reading_frames) {
   auto runtime = fcl::asio::runtime{};
   auto owner = node{runtime, options_for(peer(12))};
   auto apis = fcl::api::registry{};
   auto binding = fcl::p2p::api(owner)
                      .use(fcl::api::binding().serve(apis).build())
                      .peer_policy(api_binding::peer_policy{.require_known_peer = true})
                      .build();
   auto session_called = false;
   binding.on_session([&](fcl::api::session&) -> boost::asio::awaitable<void> {
      session_called = true;
      co_return;
   });

   auto incoming = node::incoming_protocol_stream{
       .session = node::session_info{.remote_peer = peer(13)},
       .protocol = binding.protocol(),
       .stream = fcl::quic::framed_stream{fcl::quic::stream{}},
   };

   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime, binding.accept(std::move(incoming))),
                     fcl::p2p::exceptions::peer_not_found);
   BOOST_TEST(!session_called);
}

BOOST_AUTO_TEST_CASE(p2p_codec_rejects_wrong_version_and_oversized_envelope) {
   auto message = control_message{.kind = control_message::type::ping, .peer = peer(1)};
   auto encoded = control_codec::encode(message);
   encoded[5] = 2;

   try {
      (void)control_codec::decode(encoded);
      BOOST_FAIL("expected wrong-version codec rejection");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::codec_error));
   }

   try {
      (void)control_codec::encode(
          control_message{.kind = control_message::type::ping, .payload = std::vector<std::uint8_t>(64)},
          control_codec::options{.max_message_size = 16});
      BOOST_FAIL("expected oversized codec rejection");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::codec_error));
   }
}

BOOST_AUTO_TEST_CASE(p2p_direct_nodes_negotiate_protocol_and_echo_frames) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(2))};
   auto client = node{runtime, options_for(peer(1))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   const auto session = fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));
   BOOST_TEST(session.remote_peer.value == server.local_peer().value);

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo));
   const auto payload = std::vector<std::uint8_t>{'p', '2', 'p'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().protocol_streams_opened >= 1U);
   BOOST_TEST(server.metrics().protocol_streams_accepted >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_path_manager_tries_next_direct_endpoint_after_attempt_timeout) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(64))};
   auto client = node{runtime, options_for(peer(65))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   client.peers().learn_endpoint(server.local_peer(), fcl::quic::endpoint{.host = "127.0.0.1", .port = 9},
                                 capability_set{.bits = capabilities::direct_quic});
   client.peers().learn_endpoint(server.local_peer(), server_endpoint,
                                 capability_set{.bits = capabilities::direct_quic});

   auto stream = fcl::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = false,
                                                      .timeout = std::chrono::milliseconds{2'000},
                                                      .direct_attempt_timeout = std::chrono::milliseconds{100},
                                                      .max_direct_endpoints = 2,
                                                  }));
   const auto payload = std::vector<std::uint8_t>{'d', 'i', 'r', 'e', 'c', 't'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().path_direct_attempts >= 2U);
   BOOST_TEST(client.metrics().path_direct_opens >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_successful_connect_deadline_does_not_poison_session) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(41))};
   auto client = node{runtime, options_for(peer(42))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{
                                                          .expected_peer = server.local_peer(),
                                                          .timeout = std::chrono::milliseconds{500}}));
   wait_on_runtime(runtime, std::chrono::milliseconds{700}, "post-connect deadline grace");

   auto stream = fcl::asio::blocking::run(
       runtime, client.async_open_protocol_stream(server.local_peer(), builtins::echo,
                                                  node::open_options{.timeout = std::chrono::milliseconds{1'000}}));
   const auto payload = std::vector<std::uint8_t>{'d', 'e', 'a', 'd', 'l', 'i', 'n', 'e'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());
   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_duplicate_protocol_handler_is_rejected) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto value = node{runtime, options_for(peer(3))};
   register_echo(value);

   try {
      register_echo(value);
      BOOST_FAIL("expected duplicate protocol handler rejection");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::duplicate_protocol));
   }
}

BOOST_AUTO_TEST_CASE(p2p_product_message_packs_typed_payload_as_data) {
   const auto protocol = protocol_id{.value = "/product/chunk-announce/1"};
   const auto value = product_announce{.ref = "chunk-1"};

   const auto message = fcl::p2p::message{protocol, value};

   BOOST_TEST(message.protocol().value == protocol.value);
   BOOST_TEST(message.codec().value == "fcl.raw");
   BOOST_TEST(message.as<product_announce>().ref == value.ref);
   BOOST_TEST(!message.data().empty());
}

BOOST_AUTO_TEST_CASE(p2p_api_and_route_builders_are_node_free_artifacts) {
   auto apis = fcl::api::registry{};
   auto api_binding = fcl::p2p::api()
                          .use(fcl::api::binding().serve(apis).build())
                          .protocol_id("/fcl/api/cache/1")
                          .build();

   auto route_binding = fcl::p2p::route()
                            .protocol_id("/product/blob-transfer/1")
                            .handler([](node::incoming_protocol_stream) -> boost::asio::awaitable<void> { co_return; })
                            .build();

   BOOST_TEST(api_binding.protocol().value == "/fcl/api/cache/1");
   BOOST_TEST(route_binding.protocol().value == "/product/blob-transfer/1");
}

BOOST_AUTO_TEST_CASE(p2p_connect_timeout_covers_hello_ack_wait) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto raw =
       fcl::quic::listener{runtime, fcl::quic::endpoint{.host = "127.0.0.1", .port = 0}, raw_quic_server_options()};
   auto client = node{runtime, options_for(peer(30))};

   auto server = boost::asio::co_spawn(
       runtime.context(),
       [&raw]() -> boost::asio::awaitable<void> {
          auto connection = co_await raw.async_accept();
          auto control = fcl::quic::framed_stream{
              co_await connection.async_accept_stream(),
          };
          auto request = co_await control_codec::async_read(control);
          if (request.kind != control_message::type::hello) {
             throw std::runtime_error{"raw P2P test peer expected hello"};
          }
          co_await wait_before_close(connection, std::chrono::milliseconds{500});
       },
       boost::asio::use_future);

   try {
      (void)fcl::asio::blocking::run(
          runtime,
          client.async_connect(raw.local_endpoint(),
                               node::connect_options{.expected_peer = peer(31), .timeout = std::chrono::milliseconds{100}}));
      BOOST_FAIL("expected P2P connect timeout");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::timeout));
   }

   BOOST_TEST(client.metrics().active_sessions == 0U);
   raw.stop();
   wait_for_server(server, std::chrono::milliseconds{2'000}, "raw connect-timeout peer");
   fcl::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_connect_rejects_non_positive_timeout) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto client = node{runtime, options_for(peer(32))};

   try {
      (void)fcl::asio::blocking::run(
          runtime,
          client.async_connect(fcl::quic::endpoint{.host = "127.0.0.1", .port = 9},
                               node::connect_options{.expected_peer = peer(33), .timeout = std::chrono::milliseconds{0}}));
      BOOST_FAIL("expected invalid connect timeout");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::invalid_options));
   }

   fcl::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_open_protocol_timeout_covers_missing_accept) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server_peer = peer(34);
   auto raw =
       fcl::quic::listener{runtime, fcl::quic::endpoint{.host = "127.0.0.1", .port = 0}, raw_quic_server_options()};
   auto client = node{runtime, options_for(peer(35))};

   auto server = boost::asio::co_spawn(
       runtime.context(),
       [&raw, server_peer]() -> boost::asio::awaitable<void> {
          auto connection = co_await accept_raw_p2p_session(raw, server_peer);
          auto framed = fcl::quic::framed_stream{
              co_await connection.async_accept_stream(),
          };
          auto request = co_await control_codec::async_read(framed);
          if (request.kind != control_message::type::protocol_open) {
             throw std::runtime_error{"raw P2P test peer expected protocol_open"};
          }
          co_await wait_before_close(connection, std::chrono::milliseconds{500});
       },
       boost::asio::use_future);

   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(raw.local_endpoint(), node::connect_options{.expected_peer = server_peer}));

   try {
      (void)fcl::asio::blocking::run(
          runtime, client.async_open_protocol_stream(
                       server_peer, builtins::echo,
                       node::open_options{.allow_relay = false, .timeout = std::chrono::milliseconds{100}}));
      BOOST_FAIL("expected protocol open timeout");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::timeout));
   }

   BOOST_TEST(client.metrics().active_sessions == 0U);
   raw.stop();
   wait_for_server(server, std::chrono::milliseconds{2'000}, "raw protocol-open peer");
   fcl::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_relay_open_timeout_covers_missing_accept) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto relay_peer = peer(36);
   auto target_peer = peer(37);
   auto raw =
       fcl::quic::listener{runtime, fcl::quic::endpoint{.host = "127.0.0.1", .port = 0}, raw_quic_server_options()};
   auto client = node{runtime, options_for(peer(38))};

   auto server = boost::asio::co_spawn(
       runtime.context(),
       [&raw, relay_peer]() -> boost::asio::awaitable<void> {
          auto connection = co_await accept_raw_p2p_session(raw, relay_peer);
          auto framed = fcl::quic::framed_stream{
              co_await connection.async_accept_stream(),
          };
          auto request = co_await control_codec::async_read(framed);
          if (request.kind != control_message::type::relay_reserve) {
             throw std::runtime_error{"raw P2P test peer expected relay_reserve"};
          }
          co_await wait_before_close(connection, std::chrono::milliseconds{500});
       },
       boost::asio::use_future);

   client.peers().learn_endpoint(
       relay_peer, raw.local_endpoint(),
       capability_set{.bits = capabilities::direct_quic | capabilities::relay | capabilities::relay_reservation});
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(raw.local_endpoint(), node::connect_options{.expected_peer = relay_peer}));

   try {
      (void)fcl::asio::blocking::run(
          runtime,
          client.async_open_protocol_stream(
              target_peer, builtins::echo,
              node::open_options{
                  .allow_relay = true, .relay_peer = relay_peer, .timeout = std::chrono::milliseconds{100}}));
      BOOST_FAIL("expected relay protocol open timeout");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::timeout));
   }

   raw.stop();
   wait_for_server(server, std::chrono::milliseconds{2'000}, "raw relay-timeout peer");
   fcl::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_open_protocol_rejects_non_positive_timeout) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto client = node{runtime, options_for(peer(39))};

   try {
      (void)fcl::asio::blocking::run(
          runtime, client.async_open_protocol_stream(peer(40), builtins::echo,
                                                     node::open_options{.timeout = std::chrono::milliseconds{0}}));
      BOOST_FAIL("expected invalid protocol open timeout");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::invalid_options));
   }

   fcl::asio::blocking::run(runtime, client.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_peer_exchange_learns_third_endpoint) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto third = node{runtime, options_for(peer(20))};
   auto introducer = node{runtime, options_for(peer(21))};
   auto client = node{runtime, options_for(peer(22))};

   const auto third_endpoint = listen(third, runtime);
   const auto introducer_endpoint = listen(introducer, runtime);
   introducer.peers().learn_endpoint(third.local_peer(), third_endpoint,
                                     capability_set{.bits = capabilities::direct_quic});

   (void)fcl::asio::blocking::run(
       runtime,
       client.async_connect(introducer_endpoint, node::connect_options{.expected_peer = introducer.local_peer()}));
   fcl::asio::blocking::run(runtime, client.async_request_peer_exchange(introducer.local_peer()));

   const auto learned = client.peers().find(third.local_peer());
   BOOST_REQUIRE(learned.has_value());
   BOOST_TEST(!learned->endpoints.empty());
   BOOST_TEST(learned->endpoints.front().endpoint.port == third_endpoint.port);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, introducer.async_stop());
   fcl::asio::blocking::run(runtime, third.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_explicit_relay_fallback_transfers_frames) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto target = node{runtime, options_for(peer(10))};
   auto relay_caps = capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange |
                                            capabilities::relay | capabilities::relay_reservation};
   auto relay = node{runtime, options_for(peer(11), relay_caps)};
   auto client = node{runtime, options_for(peer(12))};
   register_echo(target);

   const auto target_endpoint = listen(target, runtime);
   const auto relay_endpoint = listen(relay, runtime);
   relay.peers().learn_endpoint(target.local_peer(), target_endpoint,
                                capability_set{.bits = capabilities::direct_quic});
   client.peers().learn_endpoint(relay.local_peer(), relay_endpoint, relay_caps);

   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(relay_endpoint, node::connect_options{.expected_peer = relay.local_peer()}));
   auto stream = fcl::asio::blocking::run(
       runtime, client.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                  node::open_options{.allow_relay = true, .relay_peer = relay.local_peer()}));

   const auto payload = std::vector<std::uint8_t>{'r', 'e', 'l', 'a', 'y'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());
   fcl::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{50}, "relay normal close propagation");

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(relay.metrics().relays_opened >= 1U);
   BOOST_TEST(relay.metrics().relay_failures == 0U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, relay.async_stop());
   fcl::asio::blocking::run(runtime, target.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_path_manager_tries_next_relay_candidate_after_failure) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto target = node{runtime, options_for(peer(66))};
   auto relay_caps = capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange |
                                            capabilities::relay | capabilities::relay_reservation};
   auto relay = node{runtime, options_for(peer(68), relay_caps)};
   auto client = node{runtime, options_for(peer(69))};
   register_echo(target);

   const auto target_endpoint = listen(target, runtime);
   const auto relay_endpoint = listen(relay, runtime);
   relay.peers().learn_endpoint(target.local_peer(), target_endpoint,
                                capability_set{.bits = capabilities::direct_quic});
   client.peers().learn_endpoint(peer(67), fcl::quic::endpoint{.host = "127.0.0.1", .port = 9}, relay_caps);
   client.peers().learn_endpoint(relay.local_peer(), relay_endpoint, relay_caps);

   auto stream = fcl::asio::blocking::run(
       runtime, client.async_open_protocol_stream(target.local_peer(), builtins::echo,
                                                  node::open_options{
                                                      .allow_relay = true,
                                                      .timeout = std::chrono::milliseconds{3'000},
                                                      .relay_attempt_timeout = std::chrono::milliseconds{150},
                                                      .max_relay_candidates = 2,
                                                  }));

   const auto payload = std::vector<std::uint8_t>{'r', 'e', 't', 'r', 'y'};
   fcl::asio::blocking::run(runtime, stream.async_write_frame(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read_frame());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());
   BOOST_TEST(client.metrics().path_relay_attempts >= 2U);
   BOOST_TEST(relay.metrics().relays_opened >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, relay.async_stop());
   fcl::asio::blocking::run(runtime, target.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_reachability_probe_marks_public_loopback_peer) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto observer = node{runtime, options_for(peer(50))};
   auto client = node{runtime, options_for(peer(51))};

   const auto observer_endpoint = listen(observer, runtime);
   (void)listen(client, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(observer_endpoint, node::connect_options{.expected_peer = observer.local_peer()}));

   const auto result = fcl::asio::blocking::run(runtime, client.async_probe_reachability(observer.local_peer()));

   BOOST_TEST(static_cast<int>(result) == static_cast<int>(reachability_state::publicly_reachable));
   BOOST_TEST(observer.metrics().reachability_probes >= 1U);
   BOOST_TEST(observer.metrics().reachability_public >= 1U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, observer.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_expires_stale_reachability_observation) {
   auto store = peer_store{};
   store.upsert(peer_store::record{
       .peer = peer(70),
       .capabilities = capability_set{.bits = capabilities::direct_quic},
       .reachability = reachability_state::publicly_reachable,
       .observed_endpoint = fcl::quic::endpoint{.host = "127.0.0.1", .port = 12345},
       .reachability_expires_at = std::chrono::steady_clock::now() - std::chrono::seconds{1},
   });

   const auto record = store.find(peer(70));
   BOOST_REQUIRE(record.has_value());
   BOOST_TEST(static_cast<int>(record->reachability) == static_cast<int>(reachability_state::unknown));
   BOOST_TEST(!record->observed_endpoint.has_value());

   const auto snapshot = store.snapshot();
   BOOST_REQUIRE_EQUAL(snapshot.size(), 1U);
   BOOST_TEST(static_cast<int>(snapshot.front().reachability) == static_cast<int>(reachability_state::unknown));
}

BOOST_AUTO_TEST_CASE(p2p_relay_reservation_is_explicit_and_metered) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto relay_caps = capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange |
                                            capabilities::relay | capabilities::relay_reservation};
   auto relay = node{runtime, options_for(peer(52), relay_caps)};
   auto client = node{runtime, options_for(peer(53))};

   const auto relay_endpoint = listen(relay, runtime);
   client.peers().learn_endpoint(relay.local_peer(), relay_endpoint, relay_caps);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(relay_endpoint, node::connect_options{.expected_peer = relay.local_peer()}));

   const auto reservation = fcl::asio::blocking::run(
       runtime, client.async_reserve_relay(relay.local_peer(), relay_reservation::options{
                                                                   .ttl = std::chrono::milliseconds{5'000},
                                                                   .max_streams = 2,
                                                                   .max_bytes = 1024 * 1024,
                                                                   .max_queued_bytes = 1024 * 1024,
                                                               }));

   BOOST_TEST(reservation.id != 0U);
   BOOST_TEST(relay.metrics().active_relay_reservations == 1U);
   BOOST_TEST(relay.metrics().relay_reservations >= 1U);

   fcl::asio::blocking::run(runtime, client.async_cancel_relay(relay.local_peer()));
   BOOST_TEST(relay.metrics().active_relay_reservations == 0U);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, relay.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_hole_punch_upgrades_reserved_relay_to_direct_loopback) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 4}};
   auto target = node{
       runtime, options_for(peer(54), capability_set{.bits = capabilities::direct_quic | capabilities::hole_punching})};
   auto relay_caps = capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange |
                                            capabilities::relay | capabilities::relay_reservation};
   auto relay = node{runtime, options_for(peer(55), relay_caps)};
   auto client = node{
       runtime, options_for(peer(56), capability_set{.bits = capabilities::direct_quic | capabilities::hole_punching})};

   const auto target_endpoint = listen(target, runtime);
   const auto relay_endpoint = listen(relay, runtime);
   (void)listen(client, runtime);
   relay.peers().learn_endpoint(target.local_peer(), target_endpoint,
                                capability_set{.bits = capabilities::direct_quic | capabilities::hole_punching});
   client.peers().learn_endpoint(relay.local_peer(), relay_endpoint, relay_caps);

   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(relay_endpoint, node::connect_options{.expected_peer = relay.local_peer()}));

   const auto result =
       fcl::asio::blocking::run(runtime, client.async_attempt_hole_punch(target.local_peer(), relay.local_peer(),
                                                                         std::chrono::milliseconds{5'000}));

   BOOST_TEST(static_cast<int>(result) == static_cast<int>(hole_punch_status::succeeded));
   BOOST_TEST(client.metrics().hole_punch_successes >= 1U);
   BOOST_REQUIRE(client.peers().find(target.local_peer()).has_value());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, relay.async_stop());
   fcl::asio::blocking::run(runtime, target.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_production_options_reject_missing_mtls_identity) {
   try {
      validate(node::options{});
      BOOST_FAIL("expected missing mTLS identity rejection");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::invalid_options));
   }
}

} // namespace fcl::p2p
