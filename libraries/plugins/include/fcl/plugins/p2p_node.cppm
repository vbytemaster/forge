module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module fcl.plugins.p2p_node;

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.p2p;
import fcl.plugins.exceptions;
import fcl.quic.endpoint;
import fcl.schema;

export namespace fcl::plugins {

class p2p_node final : public fcl::app::plugin {
 public:
   struct config;
   struct delivery_id;
   struct send_options;
   struct broadcast_options;
   struct delivery_result;
   struct delivery_snapshot;
   struct outbox_record;
   enum class delivery_reliability : std::uint8_t;
   enum class path_policy : std::uint8_t;
   enum class delivery_state : std::uint8_t;
   class delivery;
   class api;
   class outbox_store;

   p2p_node();
   ~p2p_node() override;

   p2p_node(const p2p_node&) = delete;
   p2p_node& operator=(const p2p_node&) = delete;

   [[nodiscard]] static fcl::app::plugin_descriptor descriptor();

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
   std::shared_ptr<impl> impl_;
};

struct p2p_node::config {
   std::vector<std::string> listen;
   std::vector<std::string> bootstrap;
   std::vector<std::string> advertised_endpoints;
   std::string peer_id;
   std::string certificate_pem;
   std::string private_key_pem;
   std::string api_codec = "fcl.raw";
   std::uint64_t max_inflight_per_peer = 64;
   std::uint64_t max_sessions = 1024;
   std::uint64_t max_protocol_handlers = 1024;
   bool allow_insecure_test_mode = false;
   std::string delivery_outbox_mode = "memory";
   std::uint64_t delivery_queue_limit = 4096;
   std::uint64_t delivery_worker_batch = 64;
   std::string retry_reliability = "bounded-retry";
   std::uint64_t retry_max_attempts = 3;
   std::uint64_t retry_deadline_ms = 60'000;
   std::uint64_t retry_initial_backoff_ms = 250;
   std::uint64_t retry_max_backoff_ms = 30'000;
   bool retry_jitter = true;
   std::string path_policy = "direct-preferred";
   std::string relay_trust = "known-only";
   bool relay_client_enabled = true;
   bool relay_server_enabled = false;
   bool relay_public_allowed = false;
   std::uint64_t relay_reservation_ttl_ms = 60'000;
   std::uint64_t relay_max_candidates = 4;
   std::uint64_t maintenance_peer_exchange_interval_ms = 30'000;
   std::uint64_t maintenance_reachability_interval_ms = 60'000;
};

struct p2p_node::delivery_id {
   std::uint64_t value = 0;

   [[nodiscard]] friend bool operator==(const delivery_id&, const delivery_id&) = default;
};

enum class p2p_node::delivery_reliability : std::uint8_t {
   best_effort = 1,
   bounded_retry = 2,
   durable_retry = 3,
};

enum class p2p_node::path_policy : std::uint8_t {
   direct_only = 1,
   direct_preferred = 2,
   relay_only = 3,
};

enum class p2p_node::delivery_state : std::uint8_t {
   queued = 1,
   in_flight = 2,
   delivered = 3,
   failed = 4,
   expired = 5,
   cancelled = 6,
};

struct p2p_node::send_options {
   delivery_reliability reliability = delivery_reliability::bounded_retry;
   path_policy path = path_policy::direct_preferred;
   std::chrono::milliseconds deadline{60'000};
   std::chrono::milliseconds initial_backoff{250};
   std::chrono::milliseconds max_backoff{30'000};
   std::uint32_t max_attempts = 3;
   int priority = 0;
   bool allow_public_relay = false;
   bool jitter = true;
};

struct p2p_node::broadcast_options {
   std::vector<fcl::p2p::peer_id> peers;
   send_options send{};
};

struct p2p_node::delivery_result {
   delivery_id id;
   fcl::p2p::peer_id peer;
   fcl::p2p::protocol_id protocol;
   delivery_state state = delivery_state::queued;
   std::uint32_t attempts = 0;
   std::string error;
   fcl::api::error_identity error_identity;
};

struct p2p_node::delivery_snapshot {
   delivery_id id;
   fcl::p2p::peer_id peer;
   fcl::p2p::protocol_id protocol;
   delivery_state state = delivery_state::queued;
   std::uint32_t attempts = 0;
   std::string error;
   fcl::api::error_identity error_identity;
};

struct p2p_node::outbox_record {
   delivery_id id;
   fcl::p2p::peer_id peer;
   fcl::p2p::message message;
   send_options options;
   delivery_state state = delivery_state::queued;
   std::uint32_t attempts = 0;
   std::chrono::steady_clock::time_point created_at = std::chrono::steady_clock::now();
   std::chrono::steady_clock::time_point deadline_at = std::chrono::steady_clock::now() + std::chrono::milliseconds{60'000};
   std::chrono::steady_clock::time_point next_attempt_at = std::chrono::steady_clock::now();
   std::string last_error;
   fcl::api::error_identity last_error_identity;
};

class p2p_node::api {
 public:
   virtual ~api() = default;

   [[nodiscard]] static fcl::api::descriptor describe();

   [[nodiscard]] virtual fcl::p2p::peer_id local_peer() const = 0;
   [[nodiscard]] virtual std::optional<fcl::quic::endpoint> local_endpoint() const = 0;
   [[nodiscard]] virtual fcl::p2p::node_metrics metrics() const = 0;
   [[nodiscard]] virtual std::vector<fcl::p2p::peer_record> peers() const = 0;

   virtual void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) = 0;
   virtual void publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::protocol_handler handler) = 0;

   boost::asio::awaitable<delivery> send_async(fcl::p2p::peer_id peer, fcl::p2p::message message);
   virtual boost::asio::awaitable<delivery> send_async(fcl::p2p::peer_id peer, fcl::p2p::message message,
                                                       send_options options) = 0;

   boost::asio::awaitable<delivery_result> send(fcl::p2p::peer_id peer, fcl::p2p::message message);
   boost::asio::awaitable<delivery_result> send(fcl::p2p::peer_id peer, fcl::p2p::message message,
                                                send_options options);

   boost::asio::awaitable<std::vector<delivery>> broadcast_async(fcl::p2p::message message);
   virtual boost::asio::awaitable<std::vector<delivery>>
   broadcast_async(fcl::p2p::message message, broadcast_options options) = 0;
   boost::asio::awaitable<std::vector<delivery_result>> broadcast(fcl::p2p::message message);
   boost::asio::awaitable<std::vector<delivery_result>> broadcast(fcl::p2p::message message,
                                                                  broadcast_options options);

   [[nodiscard]] virtual auto delivery(delivery_id id) const -> p2p_node::delivery = 0;
   virtual boost::asio::awaitable<void> cancel(delivery_id id) = 0;

 private:
   friend class p2p_node;
   class impl;
};

class p2p_node::delivery {
 public:
   delivery();
   ~delivery();

   delivery(const delivery&) = default;
   delivery& operator=(const delivery&) = default;
   delivery(delivery&&) noexcept = default;
   delivery& operator=(delivery&&) noexcept = default;

   [[nodiscard]] delivery_id id() const noexcept;
   boost::asio::awaitable<std::optional<delivery_snapshot>> snapshot() const;
   boost::asio::awaitable<delivery_result> result() const;
   boost::asio::awaitable<void> cancel();

 private:
   friend class p2p_node::api::impl;
   class impl;

   explicit delivery(std::shared_ptr<impl> impl);

   std::shared_ptr<impl> impl_;
};

class p2p_node::outbox_store {
 public:
   virtual ~outbox_store() = default;

   [[nodiscard]] static fcl::api::descriptor describe();

   virtual boost::asio::awaitable<delivery_id> enqueue(outbox_record record) = 0;
   virtual boost::asio::awaitable<std::vector<outbox_record>>
   claim_due(std::size_t limit, std::chrono::steady_clock::time_point now) = 0;
   virtual boost::asio::awaitable<void> mark_attempt(outbox_record record) = 0;
   virtual boost::asio::awaitable<void> release(outbox_record record) = 0;
   virtual boost::asio::awaitable<void> mark_delivered(delivery_result result) = 0;
   virtual boost::asio::awaitable<void> mark_failed(delivery_result result) = 0;
   virtual boost::asio::awaitable<std::optional<delivery_snapshot>> get(delivery_id id) const = 0;
   virtual boost::asio::awaitable<void> cancel(delivery_id id) = 0;
   virtual boost::asio::awaitable<std::optional<std::chrono::steady_clock::time_point>> next_due() const = 0;
};

} // namespace fcl::plugins

BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_node::config, (),
                      (listen, bootstrap, advertised_endpoints, peer_id, certificate_pem, private_key_pem, api_codec,
                       max_inflight_per_peer, max_sessions, max_protocol_handlers, allow_insecure_test_mode,
                       delivery_outbox_mode, delivery_queue_limit, delivery_worker_batch, retry_reliability,
                       retry_max_attempts, retry_deadline_ms, retry_initial_backoff_ms, retry_max_backoff_ms,
                       retry_jitter, path_policy, relay_trust, relay_client_enabled, relay_server_enabled,
                       relay_public_allowed, relay_reservation_ttl_ms, relay_max_candidates,
                       maintenance_peer_exchange_interval_ms, maintenance_reachability_interval_ms))

export template <> struct fcl::schema::rules<fcl::plugins::p2p_node::config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::p2p_node::config> define() {
      auto schema = fcl::schema::object<fcl::plugins::p2p_node::config>();
      schema.field<&fcl::plugins::p2p_node::config::listen>("listen")
         .default_value(std::vector<std::string>{})
         .description("QUIC listen endpoints, for example quic://0.0.0.0:9443");
      schema.field<&fcl::plugins::p2p_node::config::bootstrap>("bootstrap")
         .default_value(std::vector<std::string>{})
         .description("Bootstrap peer endpoints to connect on startup");
      schema.field<&fcl::plugins::p2p_node::config::advertised_endpoints>("advertised-endpoints")
         .default_value(std::vector<std::string>{});
      schema.field<&fcl::plugins::p2p_node::config::peer_id>("peer-id").default_value("");
      schema.field<&fcl::plugins::p2p_node::config::certificate_pem>("certificate-pem").default_value("");
      schema.field<&fcl::plugins::p2p_node::config::private_key_pem>("private-key-pem").default_value("").secret();
      schema.field<&fcl::plugins::p2p_node::config::api_codec>("api-codec").default_value("fcl.raw");
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
         .description("Test-only mode for local development without mTLS material");
      schema.field<&fcl::plugins::p2p_node::config::delivery_outbox_mode>("delivery.outbox-mode")
         .default_value("memory")
         .description("Delivery outbox mode: memory, external-optional or external-required");
      schema.field<&fcl::plugins::p2p_node::config::delivery_queue_limit>("delivery.queue-limit")
         .default_value(std::uint64_t{4096})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_node::config::delivery_worker_batch>("delivery.worker-batch")
         .default_value(std::uint64_t{64})
         .range(1, 10'000);
      schema.field<&fcl::plugins::p2p_node::config::retry_reliability>("retry.reliability")
         .default_value("bounded-retry");
      schema.field<&fcl::plugins::p2p_node::config::retry_max_attempts>("retry.max-attempts")
         .default_value(std::uint64_t{3})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_node::config::retry_deadline_ms>("retry.deadline-ms")
         .default_value(std::uint64_t{60'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_node::config::retry_initial_backoff_ms>("retry.initial-backoff-ms")
         .default_value(std::uint64_t{250})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_node::config::retry_max_backoff_ms>("retry.max-backoff-ms")
         .default_value(std::uint64_t{30'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_node::config::retry_jitter>("retry.jitter").default_value(true);
      schema.field<&fcl::plugins::p2p_node::config::path_policy>("path.policy")
         .default_value("direct-preferred")
         .description("Default delivery path policy: direct-only or direct-preferred");
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
      schema.field<&fcl::plugins::p2p_node::config::maintenance_peer_exchange_interval_ms>(
         "maintenance.peer-exchange-interval-ms")
         .default_value(std::uint64_t{30'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_node::config::maintenance_reachability_interval_ms>(
         "maintenance.reachability-interval-ms")
         .default_value(std::uint64_t{60'000})
         .range(1, 86'400'000);
      return schema;
   }
};
