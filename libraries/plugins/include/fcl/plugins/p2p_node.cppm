module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <fcl/api/api_macros.hpp>
#include <fcl/exceptions/macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module fcl.plugins.p2p_node;

import fcl.api;
import fcl.api.transport;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.exceptions;
import fcl.p2p;
import fcl.schema;

export namespace fcl::plugins::p2p_node {

struct config;
struct info;
struct remote_options;
enum class path_policy : std::uint8_t;
class exceptions;
class api;
class diagnostics_source;
class pubsub_source;

class plugin final : public fcl::app::plugin {
 public:
   plugin();
   ~plugin() override;

   plugin(const plugin&) = delete;
   plugin& operator=(const plugin&) = delete;

   [[nodiscard]] fcl::app::plugin_id id() const override;
   [[nodiscard]] std::string version() const override;
   [[nodiscard]] std::optional<fcl::config::component_descriptor> describe_config() const override;
   boost::asio::awaitable<void> configure(fcl::config::component_view view) override;
   boost::asio::awaitable<void> provide(fcl::api::provider& provider) override;
   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override;
   boost::asio::awaitable<void> startup() override;
   void request_stop() noexcept override;
   boost::asio::awaitable<void> shutdown() override;

 private:
   struct impl;
   class api_impl;
   class diagnostics_source_impl;
   class pubsub_source_impl;
   std::shared_ptr<impl> impl_;
};

[[nodiscard]] fcl::app::plugin_descriptor descriptor();

class exceptions {
 public:
   enum class code : std::uint16_t {
      plugin_not_initialized = 1,
      route_conflict = 2,
      invalid_config = 3,
   };

   using plugin_not_initialized = fcl::exceptions::coded_exception<code, code::plugin_not_initialized>;
   using route_conflict = fcl::exceptions::coded_exception<code, code::route_conflict>;
   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "fcl.plugins.p2p_node")

struct config {
   std::vector<std::string> listen;
   std::vector<std::string> bootstrap;
   std::vector<std::string> advertised_endpoints;
   std::string peer_id;
   std::string certificate_pem;
   std::string private_key_pem;
   std::string api_codec = "fcl.raw";
   std::uint64_t api_deadline_ms = 0;
   std::uint64_t api_max_frame_size = 16 * 1024 * 1024;
   std::uint64_t max_inflight_per_peer = 64;
   std::uint64_t max_sessions = 1024;
   std::uint64_t max_protocol_handlers = 1024;
   bool allow_insecure_test_mode = false;
   std::string path_policy = "direct-preferred";
   std::string relay_trust = "known-only";
   bool relay_client_enabled = true;
   bool relay_server_enabled = false;
   bool relay_public_allowed = false;
   std::uint64_t relay_reservation_ttl_ms = 60'000;
   std::uint64_t relay_max_candidates = 4;
};

struct info {
   fcl::p2p::peer_id local_peer;
   std::vector<fcl::p2p::endpoint> local_endpoints;
   bool started = false;
};

struct remote_options {
   std::chrono::milliseconds open_deadline{10'000};
   std::optional<fcl::api::codec_id> codec;
   std::optional<std::size_t> max_inflight;
   std::optional<std::chrono::milliseconds> deadline;
   std::optional<std::uint32_t> max_frame_size;
};

enum class path_policy : std::uint8_t {
   direct_only = 1,
   direct_preferred = 2,
   relay_only = 3,
};

class api : public fcl::api::contract<api> {
 public:
   virtual ~api() = default;

   [[nodiscard]] virtual fcl::p2p::peer_id local_peer() const = 0;
   [[nodiscard]] virtual std::optional<fcl::p2p::endpoint> local_endpoint() const = 0;
   [[nodiscard]] virtual std::vector<fcl::p2p::endpoint> local_endpoints() const = 0;
   [[nodiscard]] virtual info network_info() const = 0;

   virtual void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) = 0;
   virtual void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol,
                            fcl::api::transport::options options) = 0;
   virtual void publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::node::protocol_handler handler) = 0;

   virtual boost::asio::awaitable<fcl::api::transport::connection>
   open_api_connection(fcl::p2p::peer_id peer, fcl::p2p::protocol_id protocol, remote_options options = {}) = 0;

   template <typename Interface>
   boost::asio::awaitable<fcl::api::handle<Interface>>
   remote(fcl::p2p::peer_id peer, fcl::p2p::protocol_id protocol, remote_options options = {}) {
      auto connection = co_await open_api_connection(std::move(peer), std::move(protocol), options);
      co_return co_await connection.template get_remote_api<Interface>();
   }

 private:
   friend class plugin;
};

class diagnostics_source : public fcl::api::contract<diagnostics_source> {
 public:
   virtual ~diagnostics_source() = default;

   [[nodiscard]] virtual fcl::p2p::diagnostics::snapshot
   snapshot(fcl::p2p::diagnostics::options options = {}) const = 0;

 private:
   friend class plugin;
};

class pubsub_source : public fcl::api::contract<pubsub_source> {
 public:
   virtual ~pubsub_source() = default;

   virtual void enable(fcl::p2p::pubsub::options options) = 0;
   [[nodiscard]] virtual fcl::p2p::peer_id local_peer() const = 0;
   virtual boost::asio::awaitable<fcl::p2p::pubsub::message>
   async_publish_message(fcl::p2p::pubsub::topic subject, std::vector<std::uint8_t> data,
                         fcl::p2p::pubsub::publish_options options) = 0;
   virtual boost::asio::awaitable<fcl::p2p::pubsub::subscription>
   async_join_topic(fcl::p2p::pubsub::topic subject, fcl::p2p::pubsub::handler handler) = 0;
   virtual boost::asio::awaitable<void> async_leave_topic(fcl::p2p::pubsub::topic subject) = 0;
   [[nodiscard]] virtual fcl::p2p::pubsub::snapshot snapshot() const = 0;

 private:
   friend class plugin;
};

} // namespace fcl::plugins::p2p_node

export {
FCL_API(::fcl::plugins::p2p_node::api, FCL_API_CONTRACT("fcl.plugins.p2p_node", 1, 0))
FCL_API(::fcl::plugins::p2p_node::diagnostics_source,
        FCL_API_CONTRACT("fcl.plugins.p2p_node.diagnostics_source", 1, 0))
FCL_API(::fcl::plugins::p2p_node::pubsub_source,
        FCL_API_CONTRACT("fcl.plugins.p2p_node.pubsub_source", 1, 0))
}

BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_node::config, (),
                      (listen, bootstrap, advertised_endpoints, peer_id, certificate_pem, private_key_pem, api_codec,
                       api_deadline_ms, api_max_frame_size, max_inflight_per_peer, max_sessions,
                       max_protocol_handlers, allow_insecure_test_mode, path_policy, relay_trust, relay_client_enabled,
                       relay_server_enabled, relay_public_allowed, relay_reservation_ttl_ms, relay_max_candidates))

export template <> struct fcl::schema::rules<fcl::plugins::p2p_node::config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::p2p_node::config> define() {
      auto schema = fcl::schema::object<fcl::plugins::p2p_node::config>();
      schema.field<&fcl::plugins::p2p_node::config::listen>("listen")
         .default_value(std::vector<std::string>{})
         .description("Listen endpoints, for example /ip4/0.0.0.0/udp/9443/quic-v1 or /ip4/0.0.0.0/tcp/4001");
      schema.field<&fcl::plugins::p2p_node::config::bootstrap>("bootstrap")
         .default_value(std::vector<std::string>{})
         .description("Bootstrap peer endpoints as libp2p multiaddr text");
      schema.field<&fcl::plugins::p2p_node::config::advertised_endpoints>("advertised-endpoints")
         .default_value(std::vector<std::string>{})
         .description("Endpoints advertised to peers as libp2p multiaddr text");
      schema.field<&fcl::plugins::p2p_node::config::peer_id>("peer-id").default_value("");
      schema.field<&fcl::plugins::p2p_node::config::certificate_pem>("certificate-pem").default_value("");
      schema.field<&fcl::plugins::p2p_node::config::private_key_pem>("private-key-pem").default_value("").secret();
      schema.field<&fcl::plugins::p2p_node::config::api_codec>("api-codec").default_value("fcl.raw");
      schema.field<&fcl::plugins::p2p_node::config::api_deadline_ms>("api.deadline-ms")
         .default_value(std::uint64_t{0})
         .range(0, 86'400'000);
      schema.field<&fcl::plugins::p2p_node::config::api_max_frame_size>("api.max-frame-size")
         .default_value(std::uint64_t{16 * 1024 * 1024})
         .range(1, 1024 * 1024 * 1024);
      schema.field<&fcl::plugins::p2p_node::config::max_inflight_per_peer>("max-inflight-per-peer")
         .default_value(std::uint64_t{64})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_node::config::max_sessions>("max-sessions")
         .default_value(std::uint64_t{1024})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_node::config::max_protocol_handlers>("max-protocol-handlers")
         .default_value(std::uint64_t{1024})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_node::config::allow_insecure_test_mode>("allow-insecure-test-mode")
         .default_value(false)
         .description("Test-only mode for local development without deployment identity material");
      schema.field<&fcl::plugins::p2p_node::config::path_policy>("path.policy")
         .default_value("direct-preferred")
         .description("Default host path policy: direct-only, direct-preferred or relay-only");
      schema.field<&fcl::plugins::p2p_node::config::relay_trust>("relay.trust")
         .default_value("known-only")
         .description("Relay trust policy: known-only or public-allowed");
      schema.field<&fcl::plugins::p2p_node::config::relay_client_enabled>("relay.client-enabled")
         .default_value(true);
      schema.field<&fcl::plugins::p2p_node::config::relay_server_enabled>("relay.server-enabled")
         .default_value(false);
      schema.field<&fcl::plugins::p2p_node::config::relay_public_allowed>("relay.public-allowed")
         .default_value(false);
      schema.field<&fcl::plugins::p2p_node::config::relay_reservation_ttl_ms>("relay.reservation-ttl-ms")
         .default_value(std::uint64_t{60'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_node::config::relay_max_candidates>("relay.max-candidates")
         .default_value(std::uint64_t{4})
         .range(1, 10'000);
      return schema;
   }
};
