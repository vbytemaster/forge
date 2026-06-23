module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module forge.p2p.node;

import forge.asio.runtime;
import forge.p2p.dht;
import forge.p2p.discovery;
import forge.p2p.diagnostics;
import forge.p2p.endpoint;
import forge.p2p.hole_punch;
import forge.p2p.identity;
import forge.p2p.peer_store;
import forge.p2p.protocol;
import forge.p2p.pubsub;
import forge.p2p.reachability;
import forge.p2p.rendezvous;
import forge.p2p.relay;
import forge.p2p.resource_manager;
import forge.p2p.scoring;
import forge.p2p.stream;
import forge.transport.limits;

export namespace forge::p2p {

class node {
 public:
   struct limits {
      std::size_t max_sessions = 1024;
      std::size_t max_pending_inbound_sessions = 1024;
      std::size_t max_pending_outbound_sessions = 1024;
      std::size_t max_inbound_sessions = 1024;
      std::size_t max_outbound_sessions = 1024;
      std::size_t max_sessions_per_peer = 4;
      std::size_t session_low_watermark = 1024;
      std::chrono::milliseconds session_grace_period{60'000};
      std::chrono::milliseconds session_prune_silence{10'000};
      std::chrono::milliseconds dial_backoff_base{5'000};
      std::chrono::milliseconds dial_backoff_step{1'000};
      std::chrono::milliseconds dial_backoff_max{300'000};
      std::size_t max_protocol_handlers = 1024;
      std::size_t max_peer_exchange_message_size = 4 * 1024 * 1024;
      std::size_t max_peer_exchange_records = 1024;
      std::size_t max_peer_exchange_queue = 4096;
      relay::limits relay{};
      resource_manager::limits resources{};
      discovery::policy discovery{};
      dht::options dht{};
      rendezvous::options rendezvous{};
      pubsub::options pubsub{};
   };

   struct options {
      std::string certificate_pem;
      std::string private_key_pem;
      std::optional<peer_id> explicit_peer_id;
      capability_set capabilities{.bits = capabilities::direct_quic | capabilities::peer_exchange};
      limits limits{};
      relay::policy relay_policy{.service_enabled = true, .client_enabled = true, .public_relay_allowed = false};
      path::policy path_policy{};
      forge::transport::limits transport_limits{};
      std::vector<forge::p2p::endpoint> advertised_endpoints;
      std::vector<std::uint8_t> public_key;
      std::string protocol_version = "/forge/1.0.0";
      std::string agent_version = "forge/1.0.0";
      std::shared_ptr<peer_store::backend> peer_store_backend;
      std::optional<std::filesystem::path> peer_store_path;
      bool allow_insecure_test_mode = false;
   };

   struct connect_options {
      std::optional<peer_id> expected_peer;
      bool allow_relay = true;
      std::optional<peer_id> relay_peer;
      std::chrono::milliseconds timeout{10'000};
      std::chrono::milliseconds direct_attempt_timeout{2'000};
      std::chrono::milliseconds relay_attempt_timeout{5'000};
      std::size_t max_direct_endpoints = 4;
      std::size_t max_relay_candidates = 4;
      bool allow_hole_punch = true;
   };

   struct open_options {
      bool allow_relay = true;
      std::optional<peer_id> relay_peer;
      std::chrono::milliseconds timeout{10'000};
      std::chrono::milliseconds direct_attempt_timeout{2'000};
      std::chrono::milliseconds relay_attempt_timeout{5'000};
      std::size_t max_direct_endpoints = 4;
      std::size_t max_relay_candidates = 4;
      bool allow_hole_punch = true;
   };

   struct session_info {
      peer_id remote_peer;
      capability_set capabilities{};
      path::kind path = path::kind::direct;
      std::optional<peer_id> relay_peer;
   };

   struct incoming_protocol_stream {
      session_info session;
      protocol_id protocol;
      forge::p2p::stream stream;
   };

   using protocol_handler = std::function<boost::asio::awaitable<void>(incoming_protocol_stream)>;

   using metrics_snapshot = diagnostics::metrics_snapshot;

   node(forge::asio::runtime& runtime, options options_value);
   ~node();

   node(const node&) = delete;
   node& operator=(const node&) = delete;

   node(node&&) noexcept;
   node& operator=(node&&) noexcept;

   [[nodiscard]] const peer_id& local_peer() const noexcept;
   [[nodiscard]] std::optional<forge::p2p::endpoint> local_endpoint() const;
   [[nodiscard]] std::vector<forge::p2p::endpoint> local_endpoints() const;
   [[nodiscard]] metrics_snapshot metrics() const;
   [[nodiscard]] forge::p2p::diagnostics::snapshot
   diagnostics(forge::p2p::diagnostics::options options = {}) const;
   [[nodiscard]] peer_store& peers() noexcept;
   [[nodiscard]] const peer_store& peers() const noexcept;

   void protect_peer(peer_id peer, std::string tag = "manual");
   [[nodiscard]] bool unprotect_peer(peer_id peer, std::string tag = "manual");
   [[nodiscard]] bool is_peer_protected(const peer_id& peer) const;

   void register_protocol_handler(protocol_id protocol, protocol_handler handler);
   boost::asio::awaitable<void> async_listen(forge::p2p::endpoint endpoint);
   boost::asio::awaitable<session_info> async_connect(forge::p2p::endpoint endpoint);
   boost::asio::awaitable<session_info> async_connect(forge::p2p::endpoint endpoint, connect_options options);
   boost::asio::awaitable<void> async_request_peer_exchange(peer_id peer);
   boost::asio::awaitable<reachability::state> async_probe_reachability(peer_id observer);
   boost::asio::awaitable<relay::reservation::info> async_reserve_relay(peer_id relay_peer);
   boost::asio::awaitable<relay::reservation::info> async_reserve_relay(peer_id relay_peer,
                                                                        relay::reservation::options options);
   boost::asio::awaitable<std::vector<relay::reservation::info>> async_refresh_relay_candidates();
   boost::asio::awaitable<std::vector<discovery::result>> async_refresh_discovery();
   boost::asio::awaitable<void> async_cancel_relay(peer_id relay_peer);
   boost::asio::awaitable<dht::query_result> async_find_peer(peer_id peer);
   boost::asio::awaitable<void> async_provide(dht::key key);
   boost::asio::awaitable<std::vector<dht::peer>> async_find_providers(dht::key key);
   boost::asio::awaitable<rendezvous::register_response>
   async_rendezvous_register(peer_id rendezvous_peer, rendezvous::register_request request);
   boost::asio::awaitable<rendezvous::discover_response>
   async_rendezvous_discover(peer_id rendezvous_peer, rendezvous::discover_request request);
   boost::asio::awaitable<pubsub::subscription> async_subscribe(pubsub::topic subject, pubsub::handler handler);
   boost::asio::awaitable<void> async_unsubscribe(pubsub::topic subject);
   boost::asio::awaitable<pubsub::message> async_publish(pubsub::topic subject, std::vector<std::uint8_t> data);
   boost::asio::awaitable<pubsub::message> async_publish(pubsub::topic subject, std::vector<std::uint8_t> data,
                                                         pubsub::publish_options options);
   [[nodiscard]] pubsub::snapshot pubsub_snapshot() const;
   boost::asio::awaitable<std::chrono::milliseconds> async_ping(peer_id peer);
   boost::asio::awaitable<std::chrono::milliseconds> async_ping(peer_id peer, open_options options);
   boost::asio::awaitable<hole_punch::status>
   async_attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer = std::nullopt,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds{10'000});
   boost::asio::awaitable<forge::p2p::stream> async_open_protocol_stream(peer_id peer, protocol_id protocol);
   boost::asio::awaitable<forge::p2p::stream> async_open_protocol_stream(peer_id peer, protocol_id protocol,
                                                                       open_options options);
   boost::asio::awaitable<void> async_stop();
   void stop();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

void validate(const node::options& options);

} // namespace forge::p2p
