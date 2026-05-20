#include <boost/test/unit_test.hpp>
#include <boost/describe.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <ranges>
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
import fcl.p2p.codec;
import fcl.p2p.endpoint;
import fcl.p2p.errors;
import fcl.p2p.exceptions;
import fcl.p2p.identify;
import fcl.p2p.identity;
import fcl.p2p.message;
import fcl.p2p.negotiation;
import fcl.p2p.node;
import fcl.p2p.peer_store;
import fcl.p2p.protocol;
import fcl.p2p.relay;
import fcl.p2p.scoring;
import fcl.p2p.stream;
import fcl.quic.endpoint;
import fcl.quic.libp2p;
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

std::filesystem::path temp_store_path(std::string_view name) {
   auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
   auto path = std::filesystem::temp_directory_path() /
               ("fcl-p2p-" + std::string{name} + "-" + std::to_string(stamp));
   std::filesystem::remove_all(path);
   return path;
}

class temp_store_dir {
 public:
   explicit temp_store_dir(std::string_view name) : path_(temp_store_path(name)) {}

   ~temp_store_dir() {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
   }

   [[nodiscard]] const std::filesystem::path& path() const noexcept {
      return path_;
   }

 private:
   std::filesystem::path path_;
};

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

class counting_peer_store_backend final : public peer_store::backend {
 public:
   void upsert(peer_store::record value) override {
      ++upsert_count;
      records[value.peer] = std::move(value);
   }

   void learn_endpoint(peer_id value, fcl::quic::endpoint endpoint, capability_set capabilities) override {
      ++learn_endpoint_count;
      auto& record = records[value];
      record.peer = std::move(value);
      record.capabilities.bits |= capabilities.bits;
      record.endpoints.push_back(peer_store::endpoint_record{.endpoint = std::move(endpoint)});
   }

   void mark_reachability(peer_id value, reachability_state state,
                          std::optional<fcl::quic::endpoint> observed) override {
      auto& record = records[value];
      record.peer = std::move(value);
      record.reachability = state;
      record.observed_endpoint = std::move(observed);
   }

   void mark_success(const peer_id& value, path::kind, std::chrono::milliseconds latency) override {
      auto& record = records[value];
      record.peer = value;
      ++record.successes;
      record.last_latency = latency;
   }

   void mark_failure(const peer_id& value) override {
      auto& record = records[value];
      record.peer = value;
      ++record.failures;
   }

   void mark_endpoint_success(const peer_id& value, const fcl::quic::endpoint& endpoint, path::kind kind,
                              std::chrono::milliseconds latency) override {
      auto& record = records[value];
      record.peer = value;
      record.endpoints.push_back(peer_store::endpoint_record{.endpoint = endpoint, .kind = kind, .last_latency = latency});
   }

   void mark_endpoint_failure(const peer_id& value, const fcl::quic::endpoint& endpoint, path::kind kind,
                              std::chrono::system_clock::time_point backoff_until) override {
      auto& record = records[value];
      record.peer = value;
      record.endpoints.push_back(peer_store::endpoint_record{.endpoint = endpoint, .kind = kind, .backoff_until = backoff_until});
   }

   [[nodiscard]] std::optional<peer_store::record> find(const peer_id& value) const override {
      const auto it = records.find(value);
      if (it == records.end()) {
         return std::nullopt;
      }
      return it->second;
   }

   [[nodiscard]] std::vector<peer_store::record> snapshot() const override {
      auto out = std::vector<peer_store::record>{};
      out.reserve(records.size());
      for (const auto& [_, record] : records) {
         out.push_back(record);
      }
      return out;
   }

   std::uint32_t upsert_count = 0;
   std::uint32_t learn_endpoint_count = 0;
   std::map<peer_id, peer_store::record> records;
};

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

BOOST_AUTO_TEST_CASE(p2p_multistream_select_encodes_libp2p_messages) {
   using namespace protocol_negotiation;

   const auto header = encode_frame(encode_message(protocol_negotiation::message{.kind = message_kind::header}));
   BOOST_TEST(header == std::vector<std::uint8_t>({19, '/', 'm', 'u', 'l', 't', 'i', 's', 't', 'r', 'e', 'a', 'm', '/',
                                                   '1', '.', '0', '.', '0', '\n'}),
              boost::test_tools::per_element());

   const auto ping = protocol_id{.value = "/ipfs/ping/1.0.0"};
   auto decoded = decode_message(decode_frame(encode_frame(encode_message(protocol_negotiation::message{
                                                                                  .kind = message_kind::protocol,
                                                                                  .protocol = ping})))
                                     .payload);
   BOOST_TEST(static_cast<int>(decoded.kind) == static_cast<int>(message_kind::protocol));
   BOOST_TEST(decoded.protocol.value == ping.value);

   auto list = decode_message(encode_message(protocol_negotiation::message{
       .kind = message_kind::protocols,
       .protocols = std::vector<protocol_id>{ping}}));
   BOOST_TEST(static_cast<int>(list.kind) == static_cast<int>(message_kind::protocols));
   BOOST_REQUIRE_EQUAL(list.protocols.size(), 1U);
   BOOST_TEST(list.protocols.front().value == ping.value);

   BOOST_CHECK_THROW((void)decode_frame(std::vector<std::uint8_t>{0x81, 0x81, 0x01}),
                     fcl::p2p::p2p_error);
   BOOST_CHECK_THROW((void)decode_message(std::vector<std::uint8_t>{'b', 'a', 'd', '\n'}),
                     fcl::p2p::p2p_error);
}

BOOST_AUTO_TEST_CASE(p2p_identify_document_roundtrips_libp2p_fields) {
   const auto id = peer(74);
   auto doc = identify::document{
       .protocol_version = "/fcl/test/1",
       .agent_version = "fcl-test/1",
       .public_key = std::vector<std::uint8_t>{1, 2, 3},
       .listen_endpoints = std::vector<endpoint>{parse_endpoint("/ip4/127.0.0.1/udp/4001/quic-v1/p2p/" + id.to_string())},
       .observed_endpoint = parse_endpoint("/ip4/127.0.0.1/udp/5001/quic-v1/p2p/" + id.to_string()),
       .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify},
       .signed_peer_record = std::vector<std::uint8_t>{9, 8, 7},
   };

   auto decoded = identify::decode(identify::encode(doc));

   BOOST_TEST(decoded.protocol_version == doc.protocol_version);
   BOOST_TEST(decoded.agent_version == doc.agent_version);
   BOOST_TEST(decoded.public_key == doc.public_key, boost::test_tools::per_element());
   BOOST_REQUIRE_EQUAL(decoded.listen_endpoints.size(), 1U);
   BOOST_TEST(decoded.listen_endpoints.front().to_string() == doc.listen_endpoints.front().to_string());
   BOOST_REQUIRE(decoded.observed_endpoint.has_value());
   BOOST_TEST(decoded.observed_endpoint->to_string() == doc.observed_endpoint->to_string());
   BOOST_REQUIRE_EQUAL(decoded.protocols.size(), 2U);
   BOOST_TEST(decoded.protocols.front().value == builtins::ping.value);
   BOOST_TEST(decoded.signed_peer_record == doc.signed_peer_record, boost::test_tools::per_element());
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

BOOST_AUTO_TEST_CASE(p2p_ping_protocol_uses_libp2p_payload_echo) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(72))};
   auto client = node{runtime, options_for(peer(73))};

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::ping));
   const auto payload = std::vector<std::uint8_t>(32, 0x42);
   fcl::asio::blocking::run(runtime, stream.async_write(payload));
   const auto reply = fcl::asio::blocking::run(runtime, stream.async_read());

   BOOST_TEST(reply == payload, boost::test_tools::per_element());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_ping_api_returns_rtt) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(82))};
   auto client = node{runtime, options_for(peer(83))};

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   const auto rtt = fcl::asio::blocking::run(runtime, client.async_ping(server.local_peer()));
   BOOST_TEST(rtt.count() >= 0);

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_protocol_advertises_supported_protocols) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(75))};
   auto client = node{runtime, options_for(peer(76))};
   register_echo(server);

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify));
   const auto payload = fcl::asio::blocking::run(runtime, stream.async_read());
   const auto doc = identify::decode(payload);

   BOOST_TEST(doc.agent_version == "fcl/0.1.0");
   BOOST_TEST(std::ranges::any_of(doc.protocols, [](const protocol_id& value) { return value == builtins::ping; }));
   BOOST_TEST(std::ranges::any_of(doc.protocols, [](const protocol_id& value) { return value == builtins::identify; }));
   BOOST_TEST(std::ranges::any_of(doc.protocols, [](const protocol_id& value) { return value == builtins::echo; }));
   BOOST_TEST(!doc.listen_endpoints.empty());

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_updates_peer_store) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(77))};
   auto client = node{runtime, options_for(peer(78))};

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
   auto pushed = identify::document{
       .protocol_version = "/fcl/push-test/1",
       .agent_version = "fcl-push-test/1",
       .listen_endpoints = std::vector<endpoint>{parse_endpoint("/ip4/127.0.0.1/udp/4101/quic-v1/p2p/" +
                                                                client.local_peer().to_string())},
       .protocols = std::vector<protocol_id>{builtins::ping},
   };
   fcl::asio::blocking::run(runtime, stream.async_write(identify::encode(pushed)));
   fcl::asio::blocking::run(runtime, stream.async_close());
   wait_on_runtime(runtime, std::chrono::milliseconds{100}, "identify push propagation");

   const auto snapshot = server.peers().snapshot();
   BOOST_TEST(std::ranges::any_of(snapshot, [](const peer_store::record& record) {
      return record.protocol_version == "/fcl/push-test/1" &&
             std::ranges::any_of(record.protocols, [](const protocol_id& protocol) { return protocol == builtins::ping; });
   }));

   fcl::asio::blocking::run(runtime, client.async_stop());
   fcl::asio::blocking::run(runtime, server.async_stop());
}

BOOST_AUTO_TEST_CASE(p2p_identify_push_persists_rocksdb_peer_record) {
   auto temp = temp_store_dir{"identify-push-rocksdb"};
   const auto client_id = peer(178);

   {
      auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
      auto server_options = options_for(peer(177));
      server_options.peer_store_path = temp.path();
      auto server = node{runtime, std::move(server_options)};
      auto client = node{runtime, options_for(client_id)};

      const auto server_endpoint = listen(server, runtime);
      (void)fcl::asio::blocking::run(
          runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

      auto stream =
          fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::identify_push));
      auto pushed = identify::document{
          .protocol_version = "/fcl/push-persist/1",
          .agent_version = "fcl-push-persist/1",
          .public_key = std::vector<std::uint8_t>{9, 8, 7},
          .listen_endpoints = std::vector<endpoint>{parse_endpoint("/ip4/127.0.0.1/udp/4201/quic-v1/p2p/" +
                                                                   client.local_peer().to_string())},
          .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify},
          .signed_peer_record = std::vector<std::uint8_t>{6, 5, 4},
      };
      fcl::asio::blocking::run(runtime, stream.async_write(identify::encode(pushed)));
      fcl::asio::blocking::run(runtime, stream.async_close());
      wait_on_runtime(runtime, std::chrono::milliseconds{100}, "identify push persistence");

      fcl::asio::blocking::run(runtime, client.async_stop());
      fcl::asio::blocking::run(runtime, server.async_stop());
   }

   auto reopened = peer_store{peer_store::options{
       .backend = peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = temp.path()}),
   }};
   const auto snapshot = reopened.snapshot();
   const auto found = std::ranges::find_if(snapshot, [](const peer_store::record& value) {
      return value.protocol_version == "/fcl/push-persist/1";
   });
   BOOST_REQUIRE(found != snapshot.end());
   BOOST_TEST(found->agent_version == "fcl-push-persist/1");
   BOOST_TEST(found->public_key == std::vector<std::uint8_t>({9, 8, 7}), boost::test_tools::per_element());
   BOOST_TEST(found->signed_peer_record == std::vector<std::uint8_t>({6, 5, 4}), boost::test_tools::per_element());
   BOOST_TEST(std::ranges::any_of(found->protocols, [](const protocol_id& value) { return value == builtins::identify; }));
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 1U);
   BOOST_TEST(found->endpoints.front().endpoint.port == 4201);
}

BOOST_AUTO_TEST_CASE(p2p_unsupported_protocol_rejection_keeps_session_usable) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto server = node{runtime, options_for(peer(79))};
   auto client = node{runtime, options_for(peer(80))};

   const auto server_endpoint = listen(server, runtime);
   (void)fcl::asio::blocking::run(
       runtime, client.async_connect(server_endpoint, node::connect_options{.expected_peer = server.local_peer()}));

   try {
      (void)fcl::asio::blocking::run(
          runtime, client.async_open_protocol_stream(server.local_peer(), protocol_id{.value = "/product/missing/1"}));
      BOOST_FAIL("expected unsupported protocol rejection");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::unsupported_protocol));
   }

   auto stream =
       fcl::asio::blocking::run(runtime, client.async_open_protocol_stream(server.local_peer(), builtins::ping));
   const auto payload = std::vector<std::uint8_t>(32, 0x24);
   fcl::asio::blocking::run(runtime, stream.async_write(payload));
   BOOST_TEST(fcl::asio::blocking::run(runtime, stream.async_read()) == payload, boost::test_tools::per_element());

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

BOOST_AUTO_TEST_CASE(p2p_peer_store_expires_stale_reachability_observation) {
   auto store = peer_store{};
   store.upsert(peer_store::record{
       .peer = peer(70),
       .capabilities = capability_set{.bits = capabilities::direct_quic},
       .reachability = reachability_state::publicly_reachable,
       .observed_endpoint = fcl::quic::endpoint{.host = "127.0.0.1", .port = 12345},
       .reachability_expires_at = std::chrono::system_clock::now() - std::chrono::seconds{1},
   });

   const auto record = store.find(peer(70));
   BOOST_REQUIRE(record.has_value());
   BOOST_TEST(static_cast<int>(record->reachability) == static_cast<int>(reachability_state::unknown));
   BOOST_TEST(!record->observed_endpoint.has_value());

   const auto snapshot = store.snapshot();
   BOOST_REQUIRE_EQUAL(snapshot.size(), 1U);
   BOOST_TEST(static_cast<int>(snapshot.front().reachability) == static_cast<int>(reachability_state::unknown));
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_uses_injected_backend) {
   auto backend = std::make_shared<counting_peer_store_backend>();
   auto store = peer_store{peer_store::options{.backend = backend}};
   const auto id = peer(81);

   store.upsert(peer_store::record{
       .peer = id,
       .protocol_version = "/fcl/test/1",
       .agent_version = "fcl-test/1",
       .protocols = std::vector<protocol_id>{builtins::ping},
   });
   store.learn_endpoint(id, fcl::quic::endpoint{.host = "127.0.0.1", .port = 4001},
                        capability_set{.bits = capabilities::direct_quic});

   BOOST_TEST(backend->upsert_count == 1U);
   BOOST_TEST(backend->learn_endpoint_count == 1U);
   const auto found = store.find(id);
   BOOST_REQUIRE(found.has_value());
   BOOST_TEST(found->protocol_version == "/fcl/test/1");
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 1U);
   BOOST_TEST(found->endpoints.front().endpoint.port == 4001);
}

BOOST_AUTO_TEST_CASE(p2p_peer_store_rocksdb_survives_reopen) {
   auto temp = temp_store_dir{"rocksdb-reopen"};
   const auto id = peer(82);
   const auto endpoint = fcl::quic::endpoint{.host = "127.0.0.1", .port = 4101};
   const auto observed = fcl::quic::endpoint{.host = "127.0.0.1", .port = 4102};

   {
      auto store = peer_store{peer_store::options{
          .backend = peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = temp.path()}),
      }};
      store.upsert(peer_store::record{
          .peer = id,
          .capabilities = capability_set{.bits = capabilities::direct_quic | capabilities::peer_exchange},
          .protocol_version = "/fcl/reopen/1",
          .agent_version = "fcl-reopen/1",
          .public_key = std::vector<std::uint8_t>{1, 2, 3, 4},
          .protocols = std::vector<protocol_id>{builtins::ping, builtins::identify},
          .signed_peer_record = std::vector<std::uint8_t>{5, 6, 7},
          .endpoints = std::vector<peer_store::endpoint_record>{peer_store::endpoint_record{
              .endpoint = endpoint,
              .kind = path::kind::direct,
              .successes = 2,
              .failures = 1,
              .last_latency = std::chrono::milliseconds{17},
              .backoff_until = std::chrono::system_clock::now() + std::chrono::seconds{30},
          }},
          .reachability = reachability_state::publicly_reachable,
          .observed_endpoint = observed,
      });
      store.mark_endpoint_failure(id, endpoint, path::kind::direct,
                                  std::chrono::system_clock::now() + std::chrono::seconds{10});
      store.mark_endpoint_success(id, endpoint, path::kind::direct, std::chrono::milliseconds{11});
   }

   auto reopened = peer_store{peer_store::options{
       .backend = peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = temp.path()}),
   }};
   const auto found = reopened.find(id);
   BOOST_REQUIRE(found.has_value());
   BOOST_TEST(found->protocol_version == "/fcl/reopen/1");
   BOOST_TEST(found->agent_version == "fcl-reopen/1");
   BOOST_TEST(found->public_key == std::vector<std::uint8_t>({1, 2, 3, 4}), boost::test_tools::per_element());
   BOOST_TEST(found->signed_peer_record == std::vector<std::uint8_t>({5, 6, 7}), boost::test_tools::per_element());
   BOOST_TEST(found->observed_endpoint.has_value());
   BOOST_TEST(found->observed_endpoint->port == observed.port);
   BOOST_REQUIRE_EQUAL(found->protocols.size(), 2U);
   BOOST_REQUIRE_EQUAL(found->endpoints.size(), 1U);
   BOOST_TEST(found->endpoints.front().endpoint.port == endpoint.port);
   BOOST_TEST(found->endpoints.front().successes >= 1U);
   BOOST_TEST(found->endpoints.front().failures >= 1U);
   BOOST_TEST(found->endpoints.front().last_latency == std::chrono::milliseconds{11});
}

BOOST_AUTO_TEST_CASE(p2p_production_options_reject_missing_mtls_identity) {
   try {
      validate(node::options{});
      BOOST_FAIL("expected missing mTLS identity rejection");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::invalid_options));
   }
}

BOOST_AUTO_TEST_CASE(p2p_production_options_require_peer_store_path) {
   try {
      validate(node::options{
          .certificate_pem = std::string{test_certificate()},
          .private_key_pem = std::string{test_private_key()},
      });
      BOOST_FAIL("expected missing persistent peer store rejection");
   } catch (const p2p_error& error) {
      BOOST_TEST(static_cast<int>(error.kind()) == static_cast<int>(error_kind::invalid_options));
   }
}

BOOST_AUTO_TEST_CASE(p2p_production_options_use_rocksdb_peer_store_path) {
   auto temp = temp_store_dir{"node-rocksdb-path"};
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   const auto id = peer(83);
   {
      auto value = node{runtime, node::options{
                                     .certificate_pem = std::string{test_certificate()},
                                     .private_key_pem = std::string{test_private_key()},
                                     .explicit_peer_id = id,
                                     .peer_store_path = temp.path(),
                                  }};
      value.peers().upsert(peer_store::record{
          .peer = peer(84),
          .protocol_version = "/fcl/node-rocksdb/1",
          .agent_version = "fcl-node-rocksdb/1",
          .protocols = std::vector<protocol_id>{builtins::identify_push},
      });
      fcl::asio::blocking::run(runtime, value.async_stop());
   }
   {
      auto value = node{runtime, node::options{
                                     .certificate_pem = std::string{test_certificate()},
                                     .private_key_pem = std::string{test_private_key()},
                                     .explicit_peer_id = id,
                                     .peer_store_path = temp.path(),
                                  }};
      const auto found = value.peers().find(peer(84));
      BOOST_REQUIRE(found.has_value());
      BOOST_TEST(found->protocol_version == "/fcl/node-rocksdb/1");
      fcl::asio::blocking::run(runtime, value.async_stop());
   }
}

} // namespace fcl::p2p
