module;

#include <boost/describe.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

export module forge.plugins.p2p.node.types;

import forge.transport.api.options;
import forge.p2p.identity;
import forge.p2p.endpoint;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;

export namespace forge::plugins::p2p::node {

enum class path_policy : std::uint8_t {
   direct_only = 1,
   direct_preferred = 2,
   relay_only = 3,
};

enum class relay_trust_policy : std::uint8_t {
   known_only = 1,
   public_allowed = 2,
};

BOOST_DESCRIBE_ENUM(path_policy, direct_only, direct_preferred, relay_only)
BOOST_DESCRIBE_ENUM(relay_trust_policy, known_only, public_allowed)

struct config {
   std::vector<std::string> listen;
   std::vector<std::string> bootstrap;
   std::vector<std::string> advertised_endpoints;
   std::string peer_id;
   std::string certificate_pem;
   std::string private_key_pem;
   std::string api_codec = "forge.raw";
   std::uint64_t api_deadline_ms = 0;
   std::uint64_t api_max_frame_size = 16 * 1024 * 1024;
   std::uint64_t max_inflight_per_peer = 64;
   std::uint64_t max_sessions = 1024;
   std::uint64_t max_protocol_handlers = 1024;
   bool allow_insecure_test_mode = false;
   forge::plugins::p2p::node::path_policy path_policy = forge::plugins::p2p::node::path_policy::direct_preferred;
   relay_trust_policy relay_trust = relay_trust_policy::known_only;
   bool relay_client_enabled = true;
   bool relay_server_enabled = false;
   bool relay_public_allowed = false;
   std::uint64_t relay_reservation_ttl_ms = 60'000;
   std::uint64_t relay_max_candidates = 4;
};

struct info {
   forge::p2p::peer_id local_peer;
   std::vector<forge::p2p::endpoint> local_endpoints;
   bool started = false;
};

struct remote_options {
   std::chrono::milliseconds open_deadline{10'000};
   std::optional<forge::api::codec_id> codec;
   std::optional<std::size_t> max_inflight;
   std::optional<std::chrono::milliseconds> deadline;
   std::optional<std::uint32_t> max_frame_size;
};

BOOST_DESCRIBE_STRUCT(config, (),
                      (listen, bootstrap, advertised_endpoints, peer_id, certificate_pem, private_key_pem, api_codec,
                       api_deadline_ms, api_max_frame_size, max_inflight_per_peer, max_sessions,
                       max_protocol_handlers, allow_insecure_test_mode, path_policy, relay_trust, relay_client_enabled,
                       relay_server_enabled, relay_public_allowed, relay_reservation_ttl_ms, relay_max_candidates))

} // namespace forge::plugins::p2p::node

export template <> struct forge::schema::rules<forge::plugins::p2p::node::config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::p2p::node::config> define() {
      auto schema = forge::schema::object<forge::plugins::p2p::node::config>();
      schema.field<&forge::plugins::p2p::node::config::listen>("listen")
         .default_value(std::vector<std::string>{})
         .description("Listen endpoints, for example /ip4/0.0.0.0/udp/9443/quic-v1 or /ip4/0.0.0.0/tcp/4001");
      schema.field<&forge::plugins::p2p::node::config::bootstrap>("bootstrap")
         .default_value(std::vector<std::string>{})
         .description("Bootstrap peer endpoints as libp2p multiaddr text");
      schema.field<&forge::plugins::p2p::node::config::advertised_endpoints>("advertised-endpoints")
         .default_value(std::vector<std::string>{})
         .description("Endpoints advertised to peers as libp2p multiaddr text");
      schema.field<&forge::plugins::p2p::node::config::peer_id>("peer-id").default_value("");
      schema.field<&forge::plugins::p2p::node::config::certificate_pem>("certificate-pem").default_value("");
      schema.field<&forge::plugins::p2p::node::config::private_key_pem>("private-key-pem").default_value("").secret();
      schema.field<&forge::plugins::p2p::node::config::api_codec>("api-codec").default_value("forge.raw");
      schema.field<&forge::plugins::p2p::node::config::api_deadline_ms>("api.deadline-ms")
         .default_value(std::uint64_t{0})
         .range(0, 86'400'000);
      schema.field<&forge::plugins::p2p::node::config::api_max_frame_size>("api.max-frame-size")
         .default_value(std::uint64_t{16 * 1024 * 1024})
         .range(1, 1024 * 1024 * 1024);
      schema.field<&forge::plugins::p2p::node::config::max_inflight_per_peer>("max-inflight-per-peer")
         .default_value(std::uint64_t{64})
         .range(1, 1'000'000);
      schema.field<&forge::plugins::p2p::node::config::max_sessions>("max-sessions")
         .default_value(std::uint64_t{1024})
         .range(1, 1'000'000);
      schema.field<&forge::plugins::p2p::node::config::max_protocol_handlers>("max-protocol-handlers")
         .default_value(std::uint64_t{1024})
         .range(1, 1'000'000);
      schema.field<&forge::plugins::p2p::node::config::allow_insecure_test_mode>("allow-insecure-test-mode")
         .default_value(false)
         .description("Test-only mode for local development without deployment identity material");
      schema.field<&forge::plugins::p2p::node::config::path_policy>("path.policy")
         .default_value("direct-preferred")
         .description("Default host path policy: direct-only, direct-preferred or relay-only");
      schema.field<&forge::plugins::p2p::node::config::relay_trust>("relay.trust")
         .default_value("known-only")
         .description("Relay trust policy: known-only or public-allowed");
      schema.field<&forge::plugins::p2p::node::config::relay_client_enabled>("relay.client-enabled")
         .default_value(true);
      schema.field<&forge::plugins::p2p::node::config::relay_server_enabled>("relay.server-enabled")
         .default_value(false);
      schema.field<&forge::plugins::p2p::node::config::relay_public_allowed>("relay.public-allowed")
         .default_value(false);
      schema.field<&forge::plugins::p2p::node::config::relay_reservation_ttl_ms>("relay.reservation-ttl-ms")
         .default_value(std::uint64_t{60'000})
         .range(1, 86'400'000);
      schema.field<&forge::plugins::p2p::node::config::relay_max_candidates>("relay.max-candidates")
         .default_value(std::uint64_t{4})
         .range(1, 10'000);
      return schema;
   }
};
