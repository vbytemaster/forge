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

export module fcl.p2p.node;

import fcl.asio.runtime;
import fcl.p2p.dht;
import fcl.p2p.discovery;
import fcl.p2p.hole_punch;
import fcl.p2p.identity;
import fcl.p2p.peer_store;
import fcl.p2p.protocol;
import fcl.p2p.reachability;
import fcl.p2p.rendezvous;
import fcl.p2p.relay;
import fcl.p2p.resource_manager;
import fcl.p2p.scoring;
import fcl.p2p.stream;
import fcl.quic.endpoint;
import fcl.quic.options;

export namespace fcl::p2p {

class node {
 public:
   struct limits {
      std::size_t max_sessions = 1024;
      std::size_t max_protocol_handlers = 1024;
      std::size_t max_peer_exchange_message_size = 4 * 1024 * 1024;
      std::size_t max_peer_exchange_records = 1024;
      std::size_t max_peer_exchange_queue = 4096;
      relay::limits relay{};
      resource_manager::limits resources{};
      discovery::policy discovery{};
      dht::options dht{};
      rendezvous::options rendezvous{};
   };

   struct options {
      std::string certificate_pem;
      std::string private_key_pem;
      std::optional<peer_id> explicit_peer_id;
      capability_set capabilities{.bits = capabilities::direct_quic | capabilities::peer_exchange};
      limits limits{};
      relay::policy relay_policy{.service_enabled = true, .client_enabled = true, .public_relay_allowed = false};
      path::policy path_policy{};
      fcl::quic::transport_limits transport_limits{};
      std::vector<fcl::quic::endpoint> advertised_endpoints;
      std::vector<std::uint8_t> public_key;
      std::string protocol_version = "/fcl/0.1.0";
      std::string agent_version = "fcl/0.1.0";
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
      fcl::p2p::stream stream;
   };

   using protocol_handler = std::function<boost::asio::awaitable<void>(incoming_protocol_stream)>;

   struct metrics_snapshot {
      std::uint64_t sessions_opened = 0;
      std::uint64_t sessions_closed = 0;
      std::uint64_t handshakes_completed = 0;
      std::uint64_t handshakes_failed = 0;
      std::uint64_t protocol_streams_opened = 0;
      std::uint64_t protocol_streams_accepted = 0;
      std::uint64_t protocol_rejections = 0;
      std::uint64_t peer_exchange_messages = 0;
      std::uint64_t reachability_checks = 0;
      std::uint64_t reachability_public = 0;
      std::uint64_t reachability_private = 0;
      std::uint64_t relays_opened = 0;
      std::uint64_t relay_rejections = 0;
      std::uint64_t relay_reservations = 0;
      std::uint64_t relay_reservation_rejections = 0;
      std::uint64_t relay_reservation_expirations = 0;
      std::uint64_t relay_bytes = 0;
      std::uint64_t hole_punch_attempts = 0;
      std::uint64_t hole_punch_successes = 0;
      std::uint64_t hole_punch_failures = 0;
      std::uint64_t path_direct_opens = 0;
      std::uint64_t path_relay_opens = 0;
      std::uint64_t path_direct_attempts = 0;
      std::uint64_t path_relay_attempts = 0;
      std::uint64_t direct_failures = 0;
      std::uint64_t relay_failures = 0;
      std::uint64_t dht_queries = 0;
      std::uint64_t dht_responses = 0;
      std::uint64_t rendezvous_registrations = 0;
      std::uint64_t rendezvous_discovers = 0;
      std::uint64_t backpressure_rejections = 0;
      std::size_t active_sessions = 0;
      std::size_t active_relays = 0;
      std::size_t active_relay_reservations = 0;
      bool stopped = false;
   };

   node(fcl::asio::runtime& runtime, options options_value);
   ~node();

   node(const node&) = delete;
   node& operator=(const node&) = delete;

   node(node&&) noexcept;
   node& operator=(node&&) noexcept;

   [[nodiscard]] const peer_id& local_peer() const noexcept;
   [[nodiscard]] std::optional<fcl::quic::endpoint> local_endpoint() const;
   [[nodiscard]] metrics_snapshot metrics() const;
   [[nodiscard]] peer_store& peers() noexcept;
   [[nodiscard]] const peer_store& peers() const noexcept;

   void register_protocol_handler(protocol_id protocol, protocol_handler handler);
   boost::asio::awaitable<void> async_listen(fcl::quic::endpoint endpoint);
   boost::asio::awaitable<session_info> async_connect(fcl::quic::endpoint endpoint);
   boost::asio::awaitable<session_info> async_connect(fcl::quic::endpoint endpoint, connect_options options);
   boost::asio::awaitable<void> async_request_peer_exchange(peer_id peer);
   boost::asio::awaitable<reachability::state> async_probe_reachability(peer_id observer);
   boost::asio::awaitable<relay::reservation::info> async_reserve_relay(peer_id relay_peer);
   boost::asio::awaitable<relay::reservation::info> async_reserve_relay(peer_id relay_peer,
                                                                        relay::reservation::options options);
   boost::asio::awaitable<void> async_cancel_relay(peer_id relay_peer);
   boost::asio::awaitable<dht::query_result> async_find_peer(peer_id peer);
   boost::asio::awaitable<void> async_provide(dht::key key);
   boost::asio::awaitable<std::vector<dht::peer>> async_find_providers(dht::key key);
   boost::asio::awaitable<rendezvous::register_response>
   async_rendezvous_register(peer_id rendezvous_peer, rendezvous::register_request request);
   boost::asio::awaitable<rendezvous::discover_response>
   async_rendezvous_discover(peer_id rendezvous_peer, rendezvous::discover_request request);
   boost::asio::awaitable<std::chrono::milliseconds> async_ping(peer_id peer);
   boost::asio::awaitable<std::chrono::milliseconds> async_ping(peer_id peer, open_options options);
   boost::asio::awaitable<hole_punch::status>
   async_attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer = std::nullopt,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds{10'000});
   boost::asio::awaitable<fcl::p2p::stream> async_open_protocol_stream(peer_id peer, protocol_id protocol);
   boost::asio::awaitable<fcl::p2p::stream> async_open_protocol_stream(peer_id peer, protocol_id protocol,
                                                                       open_options options);
   boost::asio::awaitable<void> async_stop();
   void stop();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

void validate(const node::options& options);

} // namespace fcl::p2p
