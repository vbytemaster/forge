#pragma once

#include "direct_transport.hpp"
#include "operation_deadline.hpp"
#include "peer_exchange_codec.hpp"
#include "relay_transport.hpp"

namespace fcl::p2p {

[[nodiscard]] exceptions::code p2p_code(const fcl::exceptions::base& error);
[[noreturn]] void rethrow_transport_as_p2p(const fcl::exceptions::base& error);
[[nodiscard]] bool is_orderly_stream_close(const fcl::exceptions::base& error) noexcept;
[[nodiscard]] std::uint64_t random_nonce();
[[nodiscard]] std::string bytes_key(std::span<const std::uint8_t> bytes);
boost::asio::awaitable<std::vector<std::uint8_t>>
async_read_length_delimited(fcl::p2p::stream& stream, std::vector<std::uint8_t>& buffer, std::size_t max_payload_size);
[[nodiscard]] std::vector<std::uint8_t> wrap_length_delimited(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> unwrap_length_delimited(std::span<const std::uint8_t> bytes,
                                                                std::size_t max_payload_size);
[[nodiscard]] peer_exchange_codec::options codec_for(const node::options& options) noexcept;
void validate_operation_timeout(std::chrono::milliseconds timeout, std::string_view name);
[[nodiscard]] std::chrono::milliseconds remaining_timeout(std::chrono::steady_clock::time_point started,
                                                          std::chrono::milliseconds timeout,
                                                          std::string_view operation);
[[nodiscard]] std::chrono::milliseconds attempt_timeout(std::chrono::milliseconds remaining,
                                                        std::chrono::milliseconds configured,
                                                        std::string_view operation);
[[noreturn]] void throw_operation_timeout(std::string_view operation);
void validate(const node::options& options);

struct node::impl : std::enable_shared_from_this<impl> {
   struct session_state {
      node::session_info info;
      fcl::transport::session connection;
      std::optional<fcl::p2p::endpoint> direct_endpoint;
      bool closed = false;
   };

   struct relay_reservation_state {
      peer_id owner;
      peer_id relay_peer;
      std::uint64_t id = 0;
      std::chrono::steady_clock::time_point expires_at{};
      std::size_t max_streams = 0;
      std::uint64_t max_bytes = 0;
      std::size_t max_queued_bytes = 0;
      std::size_t active_streams = 0;
      std::uint64_t bytes = 0;
      bool canceled = false;
   };

   struct pubsub_state {
      std::map<std::string, pubsub::handler> handlers;
      std::map<peer_id, std::set<std::string>> peer_topics;
      std::map<std::string, std::set<peer_id>> mesh;
      std::map<std::string, pubsub::message> cache;
      std::deque<std::string> history;
      std::map<peer_id, pubsub::score> scores;
      std::map<peer_id, std::shared_ptr<fcl::p2p::stream>> outbound_streams;
      std::map<peer_id, std::size_t> active_validations_by_peer;
      std::size_t active_validations = 0;
      std::uint64_t next_seqno = 1;
      bool heartbeat_started = false;
   };

   impl(fcl::asio::runtime& runtime_value, node::options options_value);
   fcl::asio::runtime& runtime;
   node::options options;
   peer_id local;
   direct::registry direct_registry;

   mutable std::mutex mutex;
   peer_store store;
   std::map<protocol_id, node::protocol_handler> handlers;
   std::map<peer_id, std::shared_ptr<session_state>> sessions;
   std::map<peer_id, relay_reservation_state> inbound_relay_reservations;
   std::map<peer_id, relay_reservation_state> outbound_relay_reservations;
   std::map<peer_id, std::uint64_t> pending_autonat_v2_nonces;
   std::uint64_t next_reservation_id = 1;
   resource_manager resources{options.limits.resources};
   pubsub_state pubsub_value;
   node::metrics_snapshot metrics_value;
   std::size_t active_ping_streams = 0;
   bool stopped = false;


   [[nodiscard]] std::optional<fcl::p2p::endpoint> local_endpoint_for_control() const;

   void learn_from_message(const peer_exchange_message& message);

   [[nodiscard]] identify::document local_identify_document() const;

   void learn_from_identify(const peer_id& peer, const identify::document& document);

   void remember_session(std::shared_ptr<session_state> session);

   void forget_session(const peer_id& peer);

   void forget_session(const std::shared_ptr<session_state>& session);

   [[nodiscard]] std::shared_ptr<session_state> session_for(const peer_id& peer) const;

   [[nodiscard]] std::optional<node::protocol_handler> handler_for(const protocol_id& protocol) const;

   [[nodiscard]] std::vector<protocol_id> supported_protocols() const;

   void remember_autonat_v2_nonce(const peer_id& peer, std::uint64_t nonce);

   void forget_autonat_v2_nonce(const peer_id& peer);

   [[nodiscard]] bool consume_autonat_v2_nonce(const peer_id& peer, std::uint64_t nonce);

   void increment_opened_protocol();

   void increment_protocol_accepted();

   void increment_protocol_rejected();

   void increment_peer_exchange();

   [[nodiscard]] bool begin_ping_stream();

   void finish_ping_stream();

   void increment_reachability_check(reachability::state state);

   void cleanup_expired_relay_reservations_locked();

   [[nodiscard]] bool has_outbound_relay_reservation(const peer_id& relay_peer);

   bool remember_outbound_relay_reservation(relay_reservation_state reservation);

   void remember_relay_reservation_in_store(const relay::reservation::info& info);

   [[nodiscard]] std::optional<relay_reservation_state>
   remember_inbound_relay_reservation(const peer_id& owner, relay::reservation::options request);

   bool cancel_inbound_relay_reservation(const peer_id& owner, std::uint64_t reservation_id);

   relay::status begin_relay(const peer_id& owner);

   [[nodiscard]] std::uint64_t relay_byte_limit(const peer_id& owner);

   void finish_relay(const peer_id& owner);

   bool add_relay_bytes(const peer_id& owner, std::uint64_t bytes);

   void record_path_open(path::kind kind);

   void record_path_attempt(path::kind kind);

   void record_hole_punch_result(hole_punch::status status);

   void record_direct_failure(const peer_id& peer);

   void record_relay_failure();

   void increment_dht_query();

   void increment_dht_response();

   void increment_rendezvous_registration();

   void increment_rendezvous_discover();

   void increment_pubsub_published();

   void increment_pubsub_received();

   void increment_pubsub_delivered();

   void increment_pubsub_duplicate();

   void increment_pubsub_invalid(const peer_id& peer);

   void increment_pubsub_control();

   void increment_pubsub_backpressure();

   [[nodiscard]] std::vector<std::uint8_t> next_pubsub_seqno();

   [[nodiscard]] pubsub::snapshot pubsub_snapshot() const;

   [[nodiscard]] std::vector<pubsub::subscription> local_pubsub_subscriptions() const;

   [[nodiscard]] std::vector<peer_id> pubsub_candidate_peers(const std::string& topic_value,
                                                             std::optional<peer_id> except = std::nullopt) const;

   boost::asio::awaitable<void> send_pubsub_rpc(const peer_id& peer, const pubsub::rpc& value);

   boost::asio::awaitable<void> announce_pubsub_subscriptions(const peer_id& peer);

   [[nodiscard]] bool try_begin_pubsub_validation(const peer_id& peer);

   void finish_pubsub_validation(const peer_id& peer);

   [[nodiscard]] bool pubsub_control_over_limit(const pubsub::control& value) const noexcept;

   void launch_pubsub_heartbeat();

   boost::asio::awaitable<void> pubsub_heartbeat_once();

   boost::asio::awaitable<std::shared_ptr<session_state>> connect_direct(fcl::p2p::endpoint endpoint,
                                                                         node::connect_options connect_options_value);

   boost::asio::awaitable<std::shared_ptr<session_state>> ensure_direct_session(
       const peer_id& peer, std::chrono::milliseconds timeout = node::connect_options{}.timeout,
       std::size_t max_direct_endpoints = node::connect_options{}.max_direct_endpoints,
       std::chrono::milliseconds direct_attempt_timeout = node::connect_options{}.direct_attempt_timeout);

   boost::asio::awaitable<fcl::p2p::stream> open_protocol_direct(
       const peer_id& peer, const protocol_id& protocol, std::chrono::milliseconds timeout,
       std::size_t max_direct_endpoints = node::open_options{}.max_direct_endpoints,
       std::chrono::milliseconds direct_attempt_timeout = node::open_options{}.direct_attempt_timeout);

   boost::asio::awaitable<relay::reservation::info>
   request_relay_reservation(const peer_id& relay_peer, relay::reservation::options reservation_options,
                             std::chrono::milliseconds timeout);

   boost::asio::awaitable<void> ensure_relay_reservation(const peer_id& relay_peer, std::chrono::milliseconds timeout);

   boost::asio::awaitable<std::shared_ptr<fcl::yamux::session>>
   open_relay_yamux(const peer_id& peer, const peer_id& relay_peer, std::chrono::milliseconds timeout);

   boost::asio::awaitable<fcl::p2p::stream> open_protocol_via_relay(const peer_id& peer, const protocol_id& protocol,
                                                                    const peer_id& relay_peer,
                                                                    std::chrono::milliseconds timeout);

   boost::asio::awaitable<void> request_peer_exchange(const peer_id& peer);

   void launch_accept_loop();

   boost::asio::awaitable<void> handle_inbound_connection(fcl::transport::session connection, peer_id remote);

   void launch_session_accept_loop(std::shared_ptr<session_state> session);

   boost::asio::awaitable<void> handle_incoming_stream(std::shared_ptr<session_state> session,
                                                       fcl::transport::stream raw);

   boost::asio::awaitable<void> handle_ping(fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_identify(fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_identify_push(std::shared_ptr<session_state> session, fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_autonat_v2_dial_back(std::shared_ptr<session_state> session,
                                                            fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_autonat_v2_dial_request(std::shared_ptr<session_state> session,
                                                               fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_autonat_v1(fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_relayed_yamux_stream(std::shared_ptr<session_state> session,
                                                            fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_relay_stop(std::shared_ptr<session_state> session, fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_relay_hop(std::shared_ptr<session_state> session, fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_dcutr(std::shared_ptr<session_state> session, fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_dht(std::shared_ptr<session_state> session, fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_rendezvous(std::shared_ptr<session_state> session, fcl::p2p::stream stream);

   boost::asio::awaitable<void> handle_pubsub(std::shared_ptr<session_state> session, fcl::p2p::stream stream);

   boost::asio::awaitable<bool> wait_for_direct_session(const peer_id& peer, std::chrono::milliseconds timeout);

   boost::asio::awaitable<hole_punch::status>
   run_dcutr_initiator(const peer_id& peer, std::shared_ptr<fcl::yamux::session> yamux,
                       std::chrono::milliseconds timeout);

   boost::asio::awaitable<hole_punch::status>
   serve_relayed_streams_until_hole_punch(peer_id peer, std::optional<peer_id> relay_peer,
                                          std::shared_ptr<fcl::yamux::session> yamux,
                                          std::chrono::milliseconds timeout);

   boost::asio::awaitable<void> handle_peer_exchange(fcl::p2p::stream stream, std::uint64_t request_id);

   void launch_relay_pumps(peer_id owner, fcl::p2p::stream left, fcl::p2p::stream right);

   boost::asio::awaitable<hole_punch::status> attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer,
                                                                 std::chrono::milliseconds timeout);
};

} // namespace fcl::p2p
