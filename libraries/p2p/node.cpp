module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

module fcl.p2p.node;

import fcl.p2p.codec;
import fcl.p2p.endpoint;
import fcl.p2p.identify;
import fcl.p2p.errors;
import fcl.p2p.message;
import fcl.p2p.negotiation;
import fcl.p2p.scoring;
import fcl.p2p.stream;
import fcl.crypto.random;
import fcl.quic.connection;
import fcl.quic.connector;
import fcl.quic.errors;
import fcl.quic.framed_stream;
import fcl.quic.listener;
import fcl.quic.options;
import fcl.quic.security;

namespace fcl::p2p {
namespace asio = boost::asio;

namespace {

[[nodiscard]] error_kind map_quic_error(fcl::quic::error_kind kind) noexcept {
   using quic_kind = fcl::quic::error_kind;
   switch (kind) {
   case quic_kind::invalid_endpoint:
   case quic_kind::invalid_options:
      return error_kind::invalid_options;
   case quic_kind::connect_timeout:
   case quic_kind::handshake_timeout:
   case quic_kind::idle_timeout:
      return error_kind::timeout;
   case quic_kind::peer_verification_failed:
   case quic_kind::alpn_mismatch:
   case quic_kind::tls_failed:
      return error_kind::peer_verification_failed;
   case quic_kind::frame_too_large:
   case quic_kind::malformed_frame:
      return error_kind::codec_error;
   case quic_kind::backpressure_rejected:
      return error_kind::backpressure_rejected;
   case quic_kind::connection_closed:
   case quic_kind::stream_closed:
   case quic_kind::stream_reset:
      return error_kind::closed;
   case quic_kind::canceled:
      return error_kind::canceled;
   case quic_kind::dependency_unavailable:
   case quic_kind::internal_error:
   case quic_kind::unsupported:
      return error_kind::internal_error;
   }
   return error_kind::internal_error;
}

[[noreturn]] void rethrow_quic_as_p2p(const fcl::quic::quic_error& error) {
   throw_p2p_error(map_quic_error(error.kind()), error.what());
}

[[nodiscard]] bool is_orderly_stream_close(const fcl::quic::quic_error& error) noexcept {
   return error.kind() == fcl::quic::error_kind::stream_closed;
}

[[nodiscard]] control_codec::options codec_for(const node::options& options) noexcept {
   return control_codec::options{
       .max_message_size = static_cast<std::uint32_t>(options.limits.max_control_message_size),
       .max_endpoint_records = static_cast<std::uint32_t>(options.limits.max_peer_exchange_records),
   };
}

[[nodiscard]] fcl::quic::frame_codec_options frame_codec_for(const node::options& options) noexcept {
   return fcl::quic::frame_codec_options{
       .max_frame_size = static_cast<std::uint32_t>(options.transport_limits.max_frame_size),
   };
}

[[nodiscard]] control_message make_reject(control_message::type type, std::uint64_t request_id, std::string reason) {
   return control_message{
       .kind = type,
       .request_id = request_id,
       .reason = std::move(reason),
   };
}

void validate_operation_timeout(std::chrono::milliseconds timeout, std::string_view name) {
   if (timeout.count() <= 0) {
      throw_p2p_error(error_kind::invalid_options, std::string{name} + " must be positive");
   }
}

[[nodiscard]] std::chrono::milliseconds remaining_timeout(std::chrono::steady_clock::time_point started,
                                                          std::chrono::milliseconds timeout,
                                                          std::string_view operation) {
   validate_operation_timeout(timeout, operation);
   const auto elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
   if (elapsed >= timeout) {
      throw_p2p_error(error_kind::timeout, std::string{operation} + " timed out");
   }
   return timeout - elapsed;
}

[[nodiscard]] std::chrono::milliseconds
attempt_timeout(std::chrono::milliseconds remaining, std::chrono::milliseconds configured, std::string_view operation) {
   validate_operation_timeout(remaining, operation);
   validate_operation_timeout(configured, operation);
   return std::min(remaining, configured);
}

class operation_deadline {
 public:
   operation_deadline(boost::asio::io_context& context, std::chrono::milliseconds timeout)
       : timer_(std::make_shared<asio::steady_timer>(context)),
         state_(std::make_shared<std::atomic<state_value>>(state_value::pending)) {
      validate_operation_timeout(timeout, "P2P operation timeout");
      timer_->expires_after(timeout);
   }

   operation_deadline(const operation_deadline&) = delete;
   operation_deadline& operator=(const operation_deadline&) = delete;

   ~operation_deadline() {
      cancel();
   }

   template <typename Cancel> void arm(Cancel cancel) {
      auto timer = timer_;
      auto state = state_;
      timer_->async_wait([timer, state, cancel = std::move(cancel)](boost::system::error_code ec) mutable {
         if (ec) {
            return;
         }
         auto expected = state_value::pending;
         if (!state->compare_exchange_strong(expected, state_value::timed_out, std::memory_order_acq_rel)) {
            return;
         }
         cancel();
      });
   }

   [[nodiscard]] bool finish() noexcept {
      auto expected = state_value::pending;
      if (state_->compare_exchange_strong(expected, state_value::completed, std::memory_order_acq_rel)) {
         cancel();
         return true;
      }
      cancel();
      return state_->load(std::memory_order_acquire) != state_value::timed_out;
   }

   void cancel() noexcept {
      if (!timer_) {
         return;
      }
      try {
         timer_->cancel();
      } catch (...) {
         // Timer cancellation must not escape destructor/cleanup paths.
      }
   }

   [[nodiscard]] bool timed_out() const noexcept {
      return state_->load(std::memory_order_acquire) == state_value::timed_out;
   }

 private:
   enum class state_value : std::uint8_t { pending, completed, timed_out };

   std::shared_ptr<asio::steady_timer> timer_;
   std::shared_ptr<std::atomic<state_value>> state_;
};

[[noreturn]] void throw_operation_timeout(std::string_view operation) {
   throw_p2p_error(error_kind::timeout, std::string{operation} + " timed out");
}

[[nodiscard]] std::shared_ptr<peer_store::backend> make_peer_store_backend(const node::options& options) {
   if (options.peer_store_backend) {
      return options.peer_store_backend;
   }
   if (options.peer_store_path) {
      return peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = *options.peer_store_path});
   }
   return peer_store::make_memory_backend();
}

} // namespace

void validate(const node::options& options) {
   if (!options.allow_insecure_test_mode && (options.certificate_pem.empty() || options.private_key_pem.empty())) {
      throw_p2p_error(error_kind::invalid_options, "production P2P node requires mTLS certificate and private key");
   }
   if (options.certificate_pem.empty() != options.private_key_pem.empty()) {
      throw_p2p_error(error_kind::invalid_options, "P2P certificate and private key must be provided together");
   }
   if (options.explicit_peer_id && !valid_peer_id(*options.explicit_peer_id)) {
      throw_p2p_error(error_kind::invalid_options, "invalid explicit P2P peer id");
   }
   if (options.allow_insecure_test_mode && options.certificate_pem.empty() && !options.explicit_peer_id) {
      throw_p2p_error(error_kind::invalid_options,
                      "insecure P2P test node without certificate requires explicit peer id");
   }
   if (options.peer_store_backend && options.peer_store_path) {
      throw_p2p_error(error_kind::invalid_options, "P2P peer store backend and path are mutually exclusive");
   }
   if (!options.allow_insecure_test_mode && !options.peer_store_backend && !options.peer_store_path) {
      throw_p2p_error(error_kind::invalid_options, "production P2P node requires persistent peer store path");
   }
   if (options.peer_store_path && options.peer_store_path->empty()) {
      throw_p2p_error(error_kind::invalid_options, "P2P peer store path must not be empty");
   }
   if (options.limits.max_sessions == 0 || options.limits.max_protocol_handlers == 0 ||
       options.limits.max_control_message_size == 0 || options.limits.max_peer_exchange_records == 0 ||
       options.limits.max_control_queue == 0 || options.limits.relay.max_active_relays == 0 ||
       options.limits.relay.max_reservations == 0 || options.limits.relay.max_streams_per_reservation == 0 ||
       options.limits.relay.max_relay_bytes == 0 || options.limits.relay.max_queued_bytes == 0 ||
       options.limits.relay.max_duration.count() <= 0 || options.limits.relay.reservation_ttl.count() <= 0) {
      throw_p2p_error(error_kind::invalid_options, "invalid P2P node limits");
   }
}

struct node::impl : std::enable_shared_from_this<impl> {
   struct session_state {
      node::session_info info;
      fcl::quic::connection connection;
      std::optional<fcl::quic::endpoint> direct_endpoint;
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

   impl(fcl::asio::runtime& runtime_value, node::options options_value)
       : runtime(runtime_value), options(std::move(options_value)),
         local(options.explicit_peer_id ? *options.explicit_peer_id
                                        : make_peer_id_from_certificate_pem(options.certificate_pem)),
         connector(runtime_value), store(peer_store::options{.backend = make_peer_store_backend(options)}) {}

   fcl::asio::runtime& runtime;
   node::options options;
   peer_id local;
   fcl::quic::connector connector;
   std::unique_ptr<fcl::quic::listener> listener;

   mutable std::mutex mutex;
   peer_store store;
   std::map<protocol_id, node::protocol_handler> handlers;
   std::map<peer_id, std::shared_ptr<session_state>> sessions;
   std::map<peer_id, relay_reservation_state> inbound_relay_reservations;
   std::map<peer_id, relay_reservation_state> outbound_relay_reservations;
   std::uint64_t next_reservation_id = 1;
   std::uint64_t next_control_request_id = 1;
   node::metrics_snapshot metrics_value;
   std::size_t active_ping_streams = 0;
   bool stopped = false;

   [[nodiscard]] std::optional<fcl::quic::endpoint> local_endpoint_for_control() const {
      auto lock = std::scoped_lock{mutex};
      if (listener) {
         return listener->local_endpoint();
      }
      if (!options.advertised_endpoints.empty()) {
         return options.advertised_endpoints.front();
      }
      return std::nullopt;
   }

   [[nodiscard]] std::uint64_t next_request_id() {
      auto lock = std::scoped_lock{mutex};
      return next_control_request_id++;
   }

   [[nodiscard]] control_message hello(control_message::type type) const {
      auto endpoints = std::vector<control_message::endpoint_record>{};
      endpoints.reserve(options.advertised_endpoints.size());
      for (const auto& endpoint : options.advertised_endpoints) {
         endpoints.push_back(control_message::endpoint_record{
             .peer = local,
             .endpoint = endpoint,
             .capabilities = options.capabilities,
         });
      }
      return control_message{
          .kind = type,
          .peer = local,
          .capabilities = options.capabilities,
          .max_frame_size = options.transport_limits.max_frame_size,
          .endpoints = std::move(endpoints),
      };
   }

   [[nodiscard]] fcl::quic::security_options peer_verifier(std::optional<peer_id> expected = std::nullopt) const {
      if (options.allow_insecure_test_mode) {
         return fcl::quic::security_options{.verify_peer = false};
      }
      auto security = fcl::quic::security_options{.verify_peer = true};
      if (expected) {
         security.expected_sha256_fingerprint = expected->value;
      } else {
         security.verifier = [](const fcl::quic::peer_certificate& certificate) {
            return valid_peer_id(make_peer_id_from_certificate(certificate));
         };
      }
      return security;
   }

   [[nodiscard]] fcl::quic::client_options quic_client_options(std::optional<peer_id> expected) const {
      return fcl::quic::client_options{
          .alpn = "libp2p",
          .limits = options.transport_limits,
          .security = peer_verifier(std::move(expected)),
          .certificate_pem = options.certificate_pem,
          .private_key_pem = options.private_key_pem,
      };
   }

   [[nodiscard]] fcl::quic::client_options quic_client_options(std::optional<peer_id> expected,
                                                               std::chrono::milliseconds timeout) const {
      auto out = quic_client_options(std::move(expected));
      out.connect_timeout = timeout;
      out.handshake_timeout = timeout;
      return out;
   }

   [[nodiscard]] fcl::quic::server_options quic_server_options() const {
      return fcl::quic::server_options{
          .alpn = "libp2p",
          .limits = options.transport_limits,
          .security = peer_verifier(),
          .certificate_pem = options.certificate_pem,
          .private_key_pem = options.private_key_pem,
      };
   }

   [[nodiscard]] peer_id verified_peer_id(const fcl::quic::connection& connection,
                                          const std::optional<peer_id>& expected) const {
      if (options.allow_insecure_test_mode) {
         if (expected) {
            return *expected;
         }
         if (const auto certificate = connection.peer_certificate()) {
            return make_peer_id_from_certificate(*certificate);
         }
         return peer_id{.value = "insecure-test-peer"};
      }

      const auto certificate = connection.peer_certificate();
      if (!certificate) {
         throw_p2p_error(error_kind::peer_verification_failed, "P2P session has no verified peer certificate");
      }
      const auto certificate_peer = make_peer_id_from_certificate(*certificate);
      if (expected && *expected != certificate_peer) {
         throw_p2p_error(error_kind::peer_verification_failed, "P2P peer id does not match expected peer");
      }
      return certificate_peer;
   }

   void learn_from_message(const control_message& message) {
      if (valid_peer_id(message.peer)) {
         store.upsert(peer_store::record{
             .peer = message.peer,
             .capabilities = message.capabilities,
         });
      }
      for (const auto& endpoint : message.endpoints) {
         if (valid_peer_id(endpoint.peer)) {
            store.learn_endpoint(endpoint.peer, endpoint.endpoint, endpoint.capabilities);
         }
      }
   }

   [[nodiscard]] fcl::p2p::endpoint p2p_endpoint_for(const fcl::quic::endpoint& value) const {
      return fcl::p2p::endpoint{
          .kind = fcl::p2p::endpoint::address_kind::ip4,
          .host = value.host,
          .port = value.port,
          .peer = local,
      };
   }

   [[nodiscard]] identify::document local_identify_document() const {
      auto endpoints = std::vector<fcl::p2p::endpoint>{};
      endpoints.reserve(options.advertised_endpoints.size() + 1);
      for (const auto& endpoint : options.advertised_endpoints) {
         endpoints.push_back(p2p_endpoint_for(endpoint));
      }
      if (auto endpoint = local_endpoint_for_control()) {
         endpoints.push_back(p2p_endpoint_for(*endpoint));
      }
      return identify::document{
          .protocol_version = options.protocol_version,
          .agent_version = options.agent_version,
          .listen_endpoints = std::move(endpoints),
          .protocols = supported_protocols(),
      };
   }

   void learn_from_identify(const peer_id& peer, const identify::document& document) {
      auto record = store.find(peer).value_or(peer_store::record{.peer = peer});
      record.protocol_version = document.protocol_version;
      record.agent_version = document.agent_version;
      record.public_key = document.public_key;
      record.protocols = document.protocols;
      record.signed_peer_record = document.signed_peer_record;
      record.observed_endpoint = document.observed_endpoint ? std::make_optional(document.observed_endpoint->quic_endpoint())
                                                            : record.observed_endpoint;
      for (const auto& endpoint : document.listen_endpoints) {
         const auto quic_endpoint = endpoint.quic_endpoint();
         const auto exists = std::ranges::any_of(record.endpoints, [&](const peer_store::endpoint_record& current) {
            return current.endpoint.host == quic_endpoint.host && current.endpoint.port == quic_endpoint.port;
         });
         if (!exists) {
            record.endpoints.push_back(peer_store::endpoint_record{
                .endpoint = quic_endpoint,
                .kind = path::kind::direct,
            });
         }
      }
      store.upsert(std::move(record));
   }

   void remember_session(std::shared_ptr<session_state> session) {
      auto lock = std::scoped_lock{mutex};
      if (sessions.size() >= options.limits.max_sessions && !sessions.contains(session->info.remote_peer)) {
         ++metrics_value.backpressure_rejections;
         throw_p2p_error(error_kind::backpressure_rejected, "P2P max sessions reached");
      }
      sessions[session->info.remote_peer] = std::move(session);
      metrics_value.active_sessions = sessions.size();
      ++metrics_value.sessions_opened;
      ++metrics_value.handshakes_completed;
   }

   void forget_session(const peer_id& peer) {
      auto lock = std::scoped_lock{mutex};
      if (sessions.erase(peer) != 0) {
         metrics_value.active_sessions = sessions.size();
         ++metrics_value.sessions_closed;
      }
   }

   [[nodiscard]] std::shared_ptr<session_state> session_for(const peer_id& peer) const {
      auto lock = std::scoped_lock{mutex};
      const auto it = sessions.find(peer);
      if (it == sessions.end()) {
         return {};
      }
      return it->second;
   }

   [[nodiscard]] std::optional<node::protocol_handler> handler_for(const protocol_id& protocol) const {
      auto lock = std::scoped_lock{mutex};
      const auto it = handlers.find(protocol);
      if (it == handlers.end()) {
         return std::nullopt;
      }
      return it->second;
   }

   [[nodiscard]] std::vector<protocol_id> supported_protocols() const {
      auto lock = std::scoped_lock{mutex};
      auto out = std::vector<protocol_id>{builtins::ping, builtins::identify, builtins::identify_push};
      out.reserve(out.size() + handlers.size());
      for (const auto& [protocol, _] : handlers) {
         out.push_back(protocol);
      }
      return out;
   }

   void increment_protocol_opened() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.protocol_streams_opened;
   }

   void increment_protocol_accepted() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.protocol_streams_accepted;
   }

   void increment_protocol_rejected() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.protocol_rejections;
   }

   void increment_peer_exchange() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.peer_exchange_messages;
   }

   [[nodiscard]] bool begin_ping_stream() {
      auto lock = std::scoped_lock{mutex};
      if (active_ping_streams >= 2) {
         ++metrics_value.backpressure_rejections;
         ++metrics_value.protocol_rejections;
         return false;
      }
      ++active_ping_streams;
      return true;
   }

   void finish_ping_stream() {
      auto lock = std::scoped_lock{mutex};
      if (active_ping_streams > 0) {
         --active_ping_streams;
      }
   }

   void increment_reachability_probe(reachability_state state) {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.reachability_probes;
      if (state == reachability_state::publicly_reachable) {
         ++metrics_value.reachability_public;
      } else if (state == reachability_state::private_network || state == reachability_state::blocked ||
                 state == reachability_state::relay_only) {
         ++metrics_value.reachability_private;
      }
   }

   void cleanup_expired_relay_reservations_locked() {
      const auto now = std::chrono::steady_clock::now();
      for (auto it = inbound_relay_reservations.begin(); it != inbound_relay_reservations.end();) {
         if (it->second.canceled || it->second.expires_at <= now) {
            if (metrics_value.active_relay_reservations > 0) {
               --metrics_value.active_relay_reservations;
            }
            ++metrics_value.relay_reservation_expirations;
            it = inbound_relay_reservations.erase(it);
         } else {
            ++it;
         }
      }
      for (auto it = outbound_relay_reservations.begin(); it != outbound_relay_reservations.end();) {
         if (it->second.canceled || it->second.expires_at <= now) {
            it = outbound_relay_reservations.erase(it);
         } else {
            ++it;
         }
      }
   }

   [[nodiscard]] bool has_outbound_relay_reservation(const peer_id& relay_peer) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      return outbound_relay_reservations.contains(relay_peer);
   }

   bool remember_outbound_relay_reservation(relay_reservation_state reservation) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      outbound_relay_reservations[reservation.relay_peer] = std::move(reservation);
      return true;
   }

   [[nodiscard]] std::optional<relay_reservation_state> remember_inbound_relay_reservation(const peer_id& owner,
                                                                                           const control_message& request) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      if (inbound_relay_reservations.size() >= options.limits.relay.max_reservations &&
          !inbound_relay_reservations.contains(owner)) {
         ++metrics_value.relay_reservation_rejections;
         return std::nullopt;
      }
      const auto requested_ttl = request.ttl_ms > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
                                     ? options.limits.relay.reservation_ttl
                                     : std::chrono::milliseconds{static_cast<std::int64_t>(request.ttl_ms)};
      const auto ttl = request.ttl_ms == 0 ? options.limits.relay.reservation_ttl
                                           : std::min(requested_ttl, options.limits.relay.reservation_ttl);
      const auto streams = request.max_streams == 0
                               ? options.limits.relay.max_streams_per_reservation
                               : std::min<std::size_t>(static_cast<std::size_t>(request.max_streams),
                                                       options.limits.relay.max_streams_per_reservation);
      const auto bytes = request.max_bytes == 0
                             ? options.limits.relay.max_relay_bytes
                             : std::min<std::uint64_t>(request.max_bytes, options.limits.relay.max_relay_bytes);
      const auto queued = request.max_queued_bytes == 0
                              ? options.limits.relay.max_queued_bytes
                              : std::min<std::size_t>(static_cast<std::size_t>(request.max_queued_bytes),
                                                      options.limits.relay.max_queued_bytes);
      auto reservation = relay_reservation_state{
          .owner = owner,
          .relay_peer = local,
          .id = next_reservation_id++,
          .expires_at = std::chrono::steady_clock::now() + ttl,
          .max_streams = streams,
          .max_bytes = bytes,
          .max_queued_bytes = queued,
      };
      inbound_relay_reservations[owner] = reservation;
      metrics_value.active_relay_reservations = inbound_relay_reservations.size();
      ++metrics_value.relay_reservations;
      return reservation;
   }

   bool cancel_inbound_relay_reservation(const peer_id& owner, std::uint64_t reservation_id) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      const auto it = inbound_relay_reservations.find(owner);
      if (it == inbound_relay_reservations.end() || (reservation_id != 0 && it->second.id != reservation_id)) {
         return false;
      }
      inbound_relay_reservations.erase(it);
      metrics_value.active_relay_reservations = inbound_relay_reservations.size();
      return true;
   }

   bool begin_relay(const peer_id& owner) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      if (metrics_value.active_relays >= options.limits.relay.max_active_relays) {
         ++metrics_value.relay_rejections;
         return false;
      }
      if (options.limits.relay.require_reservation) {
         const auto reservation = inbound_relay_reservations.find(owner);
         if (reservation == inbound_relay_reservations.end() ||
             reservation->second.active_streams >= reservation->second.max_streams ||
             reservation->second.bytes >= reservation->second.max_bytes) {
            ++metrics_value.relay_rejections;
            return false;
         }
         ++reservation->second.active_streams;
      }
      ++metrics_value.active_relays;
      ++metrics_value.relays_opened;
      return true;
   }

   void finish_relay(const peer_id& owner) {
      auto lock = std::scoped_lock{mutex};
      auto reservation = inbound_relay_reservations.find(owner);
      if (reservation != inbound_relay_reservations.end() && reservation->second.active_streams > 0) {
         --reservation->second.active_streams;
      }
      if (metrics_value.active_relays > 0) {
         --metrics_value.active_relays;
      }
   }

   bool add_relay_bytes(const peer_id& owner, std::uint64_t bytes) {
      auto lock = std::scoped_lock{mutex};
      metrics_value.relay_bytes += bytes;
      auto reservation = inbound_relay_reservations.find(owner);
      if (reservation == inbound_relay_reservations.end()) {
         return !options.limits.relay.require_reservation;
      }
      if (reservation->second.bytes + bytes > reservation->second.max_bytes) {
         ++metrics_value.relay_rejections;
         return false;
      }
      reservation->second.bytes += bytes;
      return true;
   }

   void record_path_open(path::kind kind) {
      auto lock = std::scoped_lock{mutex};
      if (kind == path::kind::direct) {
         ++metrics_value.path_direct_opens;
      } else {
         ++metrics_value.path_relay_opens;
      }
   }

   void record_path_attempt(path::kind kind) {
      auto lock = std::scoped_lock{mutex};
      if (kind == path::kind::direct) {
         ++metrics_value.path_direct_attempts;
      } else {
         ++metrics_value.path_relay_attempts;
      }
   }

   void record_hole_punch_result(hole_punch_status status) {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.hole_punch_attempts;
      if (status == hole_punch_status::succeeded) {
         ++metrics_value.hole_punch_successes;
      } else if (status == hole_punch_status::failed) {
         ++metrics_value.hole_punch_failures;
      }
   }

   void record_direct_failure(const peer_id& peer) {
      store.mark_failure(peer);
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.direct_failures;
   }

   void record_relay_failure() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.relay_failures;
   }

   boost::asio::awaitable<std::shared_ptr<session_state>> connect_direct(fcl::quic::endpoint endpoint,
                                                                         node::connect_options connect_options_value) {
      validate_operation_timeout(connect_options_value.timeout, "P2P connect timeout");
      auto deadline = std::unique_ptr<operation_deadline>{};
      auto endpoint_copy = endpoint;
      try {
         auto started = std::chrono::steady_clock::now();
         auto connection = std::make_shared<fcl::quic::connection>(co_await connector.async_connect(
             std::move(endpoint),
             quic_client_options(connect_options_value.expected_peer, connect_options_value.timeout)));
         deadline = std::make_unique<operation_deadline>(
             runtime.context(), remaining_timeout(started, connect_options_value.timeout, "P2P connect"));
         deadline->arm([connection] { connection->cancel(); });
         if (!deadline->finish()) {
            throw_operation_timeout("P2P connect");
         }
         const auto remote = verified_peer_id(*connection, connect_options_value.expected_peer);
         store.mark_endpoint_success(
            remote, endpoint_copy, path::kind::direct,
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started));
         auto session = std::make_shared<session_state>(session_state{
            .info = node::session_info{.remote_peer = remote, .capabilities = options.capabilities, .path = path::kind::direct},
             .connection = std::move(*connection),
             .direct_endpoint = endpoint_copy,
         });
         remember_session(session);
         launch_session_accept_loop(session);
         co_return session;
      } catch (const fcl::quic::quic_error& error) {
         if (deadline && deadline->timed_out()) {
            throw_operation_timeout("P2P connect");
         }
         rethrow_quic_as_p2p(error);
      } catch (const p2p_error&) {
         if (deadline && deadline->timed_out()) {
            throw_operation_timeout("P2P connect");
         }
         throw;
      }
   }

   boost::asio::awaitable<std::shared_ptr<session_state>>
   ensure_direct_session(const peer_id& peer, std::chrono::milliseconds timeout = node::connect_options{}.timeout,
                         std::size_t max_direct_endpoints = node::connect_options{}.max_direct_endpoints,
                         std::chrono::milliseconds direct_attempt_timeout = node::connect_options{}.direct_attempt_timeout) {
      if (auto existing = session_for(peer)) {
         co_return existing;
      }
      const auto record = store.find(peer);
      if (!record || record->endpoints.empty()) {
         throw_p2p_error(error_kind::peer_not_found, "P2P peer has no known direct endpoint");
      }
      if (max_direct_endpoints == 0) {
         throw_p2p_error(error_kind::invalid_options, "P2P max direct endpoints must be positive");
      }
      auto endpoints = record->endpoints;
      const auto now = std::chrono::system_clock::now();
      auto preferred = std::vector<peer_store::endpoint_record>{};
      for (const auto& endpoint : endpoints) {
         if (endpoint.kind != path::kind::direct || endpoint.relay_peer) {
            continue;
         }
         if (endpoint.backoff_until != std::chrono::system_clock::time_point{} && endpoint.backoff_until > now) {
            continue;
         }
         preferred.push_back(endpoint);
      }
      if (preferred.empty()) {
         for (const auto& endpoint : endpoints) {
            if (endpoint.kind == path::kind::direct && !endpoint.relay_peer) {
               preferred.push_back(endpoint);
            }
         }
      }
      std::stable_sort(preferred.begin(), preferred.end(),
                       [](const auto& left, const auto& right) { return left.score > right.score; });

      const auto started = std::chrono::steady_clock::now();
      auto last_kind = std::optional<error_kind>{};
      auto last_message = std::string{};
      const auto attempts = std::min(max_direct_endpoints, preferred.size());
      for (std::size_t index = 0; index < attempts; ++index) {
         const auto remaining = remaining_timeout(started, timeout, "P2P direct path");
         const auto per_attempt = attempt_timeout(remaining, direct_attempt_timeout, "P2P direct path attempt");
         const auto endpoint = preferred[index].endpoint;
         record_path_attempt(path::kind::direct);
         try {
            co_return co_await connect_direct(
                endpoint, node::connect_options{.expected_peer = peer, .allow_relay = false, .timeout = per_attempt});
         } catch (const p2p_error& error) {
            last_kind = error.kind();
            last_message = error.what();
            store.mark_endpoint_failure(peer, endpoint, path::kind::direct,
                                        std::chrono::system_clock::now() + std::chrono::seconds{5});
            record_direct_failure(peer);
         }
      }
      if (last_kind) {
         throw_p2p_error(*last_kind, last_message);
      }
      throw_p2p_error(error_kind::peer_not_found, "P2P peer has no direct endpoint outside backoff");
   }

   boost::asio::awaitable<fcl::p2p::stream>
   open_protocol_direct(const peer_id& peer, const protocol_id& protocol, std::chrono::milliseconds timeout,
                        std::size_t max_direct_endpoints = node::open_options{}.max_direct_endpoints,
                        std::chrono::milliseconds direct_attempt_timeout = node::open_options{}.direct_attempt_timeout) {
      const auto started = std::chrono::steady_clock::now();
      auto last_kind = std::optional<error_kind>{};
      auto last_message = std::string{};
      for (std::size_t attempt = 0; attempt < max_direct_endpoints; ++attempt) {
         const auto remaining = remaining_timeout(started, timeout, "P2P protocol open");
         auto session = co_await ensure_direct_session(peer, remaining, max_direct_endpoints, direct_attempt_timeout);
         auto deadline = operation_deadline{
             runtime.context(), attempt_timeout(remaining, direct_attempt_timeout, "P2P protocol open direct attempt")};
         deadline.arm([session] { session->connection.cancel(); });
         record_path_attempt(path::kind::direct);
         try {
            auto selected =
                co_await protocol_negotiation::async_select(co_await session->connection.async_open_stream(), protocol);
            if (!deadline.finish()) {
               throw_operation_timeout("P2P protocol open");
            }
            increment_protocol_opened();
            record_path_open(path::kind::direct);
            co_return selected;
         } catch (const fcl::quic::quic_error& error) {
            session->closed = true;
            forget_session(peer);
            if (session->direct_endpoint) {
               store.mark_endpoint_failure(peer, *session->direct_endpoint, path::kind::direct,
                                           std::chrono::system_clock::now() + std::chrono::seconds{5});
            }
            record_direct_failure(peer);
            if (deadline.timed_out()) {
               last_kind = error_kind::timeout;
               last_message = "P2P protocol open timed out";
               continue;
            }
            last_kind = map_quic_error(error.kind());
            last_message = error.what();
            continue;
         } catch (const p2p_error& error) {
            if (!deadline.finish() || deadline.timed_out()) {
               session->closed = true;
               forget_session(peer);
               if (session->direct_endpoint) {
                  store.mark_endpoint_failure(peer, *session->direct_endpoint, path::kind::direct,
                                              std::chrono::system_clock::now() + std::chrono::seconds{5});
               }
               record_direct_failure(peer);
               last_kind = error_kind::timeout;
               last_message = "P2P protocol open timed out";
               continue;
            }
            if (error.kind() == error_kind::unsupported_protocol || error.kind() == error_kind::protocol_error ||
                error.kind() == error_kind::codec_error) {
               throw;
            }
            session->closed = true;
            forget_session(peer);
            if (session->direct_endpoint) {
               store.mark_endpoint_failure(peer, *session->direct_endpoint, path::kind::direct,
                                           std::chrono::system_clock::now() + std::chrono::seconds{5});
            }
            record_direct_failure(peer);
            last_kind = error.kind();
            last_message = error.what();
            continue;
         }
      }
      if (last_kind) {
         throw_p2p_error(*last_kind, last_message);
      }
      throw_p2p_error(error_kind::peer_not_found, "P2P direct path attempts were exhausted");
   }

   boost::asio::awaitable<relay_reservation::info>
   request_relay_reservation(const peer_id& relay_peer, relay_reservation::options reservation_options,
                             std::chrono::milliseconds timeout) {
      validate_operation_timeout(timeout, "P2P relay reservation timeout");
      if (reservation_options.ttl.count() <= 0 || reservation_options.max_streams == 0 ||
          reservation_options.max_bytes == 0 || reservation_options.max_queued_bytes == 0) {
         throw_p2p_error(error_kind::invalid_options, "invalid P2P relay reservation options");
      }
      const auto started = std::chrono::steady_clock::now();
      auto relay_session = co_await ensure_direct_session(relay_peer, timeout);
      auto deadline =
          operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P relay reservation")};
      deadline.arm([relay_session] { relay_session->connection.cancel(); });
      try {
         auto framed = fcl::quic::framed_stream{
             co_await relay_session->connection.async_open_stream(),
             frame_codec_for(options),
         };
         co_await control_codec::async_write(
             framed,
             control_message{
                 .kind = control_message::type::relay_reserve,
                 .request_id = next_request_id(),
                 .peer = local,
                 .ttl_ms = static_cast<std::uint64_t>(reservation_options.ttl.count()),
                 .max_streams = static_cast<std::uint64_t>(reservation_options.max_streams),
                 .max_bytes = reservation_options.max_bytes,
                 .max_queued_bytes = static_cast<std::uint64_t>(reservation_options.max_queued_bytes),
             },
             codec_for(options));
         auto response = co_await control_codec::async_read(framed, codec_for(options));
         if (!deadline.finish()) {
            throw_operation_timeout("P2P relay reservation");
         }
         if (response.kind != control_message::type::relay_reserved) {
            throw_p2p_error(response.kind == control_message::type::relay_reject ? error_kind::relay_rejected
                                                                        : error_kind::protocol_error,
                            response.reason.empty() ? "P2P relay reservation rejected" : response.reason);
         }
         auto info = relay_reservation::info{
             .relay_peer = relay_peer,
             .id = response.reservation_id,
             .ttl = std::chrono::milliseconds{static_cast<std::int64_t>(response.ttl_ms)},
             .max_streams = static_cast<std::size_t>(response.max_streams),
             .max_bytes = response.max_bytes,
             .max_queued_bytes = static_cast<std::size_t>(response.max_queued_bytes),
         };
         remember_outbound_relay_reservation(relay_reservation_state{
             .owner = local,
             .relay_peer = relay_peer,
             .id = info.id,
             .expires_at = std::chrono::steady_clock::now() + info.ttl,
             .max_streams = info.max_streams,
             .max_bytes = info.max_bytes,
             .max_queued_bytes = info.max_queued_bytes,
         });
         co_return info;
      } catch (const fcl::quic::quic_error& error) {
         if (deadline.timed_out()) {
            relay_session->closed = true;
            forget_session(relay_peer);
            throw_operation_timeout("P2P relay reservation");
         }
         rethrow_quic_as_p2p(error);
      } catch (const p2p_error&) {
         if (deadline.timed_out()) {
            relay_session->closed = true;
            forget_session(relay_peer);
            throw_operation_timeout("P2P relay reservation");
         }
         throw;
      }
   }

   boost::asio::awaitable<void> ensure_relay_reservation(const peer_id& relay_peer, std::chrono::milliseconds timeout) {
      if (has_outbound_relay_reservation(relay_peer)) {
         co_return;
      }
      (void)co_await request_relay_reservation(relay_peer,
                                               relay_reservation::options{
                                                   .ttl = options.limits.relay.reservation_ttl,
                                                   .max_streams = options.limits.relay.max_streams_per_reservation,
                                                   .max_bytes = options.limits.relay.max_relay_bytes,
                                                   .max_queued_bytes = options.limits.relay.max_queued_bytes,
                                               },
                                               timeout);
   }

   boost::asio::awaitable<fcl::quic::framed_stream> open_protocol_via_relay(const peer_id& peer,
                                                                            const protocol_id& protocol,
                                                                            const peer_id& relay_peer,
                                                                            std::chrono::milliseconds timeout) {
      const auto started = std::chrono::steady_clock::now();
      record_path_attempt(path::kind::relay);
      co_await ensure_relay_reservation(relay_peer, timeout);
      auto relay_session = co_await ensure_direct_session(relay_peer, timeout);
      auto deadline =
          operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P relay protocol open")};
      deadline.arm([relay_session] { relay_session->connection.cancel(); });
      try {
         auto framed = fcl::quic::framed_stream{
             co_await relay_session->connection.async_open_stream(),
             frame_codec_for(options),
         };
         co_await control_codec::async_write(framed,
                                      control_message{
                                          .kind = control_message::type::relay_open,
                                          .peer = peer,
                                          .protocol = protocol,
                                      },
                                      codec_for(options));
         auto response = co_await control_codec::async_read(framed, codec_for(options));
         if (!deadline.finish()) {
            throw_operation_timeout("P2P relay protocol open");
         }
         if (response.kind != control_message::type::relay_accept) {
            throw_p2p_error(response.kind == control_message::type::relay_reject ? error_kind::relay_rejected
                                                                        : error_kind::protocol_error,
                            response.reason.empty() ? "P2P relay open rejected" : response.reason);
         }
         record_path_open(path::kind::relay);
         co_return framed;
      } catch (const fcl::quic::quic_error& error) {
         record_relay_failure();
         if (deadline.timed_out()) {
            relay_session->closed = true;
            forget_session(relay_peer);
            throw_operation_timeout("P2P relay protocol open");
         }
         rethrow_quic_as_p2p(error);
      } catch (const p2p_error&) {
         record_relay_failure();
         if (deadline.timed_out()) {
            relay_session->closed = true;
            forget_session(relay_peer);
            throw_operation_timeout("P2P relay protocol open");
         }
         throw;
      }
   }

   boost::asio::awaitable<void> request_peer_exchange(const peer_id& peer) {
      auto session = co_await ensure_direct_session(peer);
      try {
         auto framed = fcl::quic::framed_stream{
             co_await session->connection.async_open_stream(),
             frame_codec_for(options),
         };
         co_await control_codec::async_write(framed,
                                      control_message{
                                          .kind = control_message::type::peer_exchange_request,
                                          .peer = local,
                                      },
                                      codec_for(options));
         auto response = co_await control_codec::async_read(framed, codec_for(options));
         if (response.kind != control_message::type::peer_exchange_response) {
            throw_p2p_error(error_kind::protocol_error, "P2P peer exchange expected response");
         }
         learn_from_message(response);
         increment_peer_exchange();
      } catch (const fcl::quic::quic_error& error) {
         rethrow_quic_as_p2p(error);
      }
   }

   void launch_accept_loop() {
      auto self = shared_from_this();
      asio::co_spawn(
          runtime.context(),
          [self]() -> asio::awaitable<void> {
             while (true) {
                {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped || !self->listener) {
                      co_return;
                   }
                }
                try {
                   auto connection = co_await self->listener->async_accept();
                   asio::co_spawn(
                       self->runtime.context(),
                       [self, connection = std::move(connection)]() mutable -> asio::awaitable<void> {
                          co_await self->handle_inbound_connection(std::move(connection));
                       },
                       asio::detached);
                } catch (...) {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped) {
                      co_return;
                   }
                   ++self->metrics_value.handshakes_failed;
                }
             }
          },
          asio::detached);
   }

   boost::asio::awaitable<void> handle_inbound_connection(fcl::quic::connection connection) {
      try {
         const auto remote = verified_peer_id(connection, std::nullopt);
         auto session = std::make_shared<session_state>(session_state{
             .info =
                 node::session_info{.remote_peer = remote, .capabilities = options.capabilities, .path = path::kind::direct},
             .connection = std::move(connection),
         });
         remember_session(session);
         launch_session_accept_loop(session);
      } catch (...) {
         // The listener owns detached accepts; failed handshakes are reflected in metrics.
         auto lock = std::scoped_lock{mutex};
         ++metrics_value.handshakes_failed;
      }
      co_return;
   }

   void launch_session_accept_loop(std::shared_ptr<session_state> session) {
      auto self = shared_from_this();
      asio::co_spawn(
          runtime.context(),
          [self, session = std::move(session)]() mutable -> asio::awaitable<void> {
             while (true) {
                {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped || session->closed) {
                      co_return;
                   }
                }
                try {
                   auto stream = co_await session->connection.async_accept_stream();
                   asio::co_spawn(
                       self->runtime.context(),
                       [self, session, stream = std::move(stream)]() mutable -> asio::awaitable<void> {
                          co_await self->handle_incoming_stream(session, std::move(stream));
                       },
                       asio::detached);
                } catch (...) {
                   session->closed = true;
                   self->forget_session(session->info.remote_peer);
                   co_return;
                }
             }
          },
          asio::detached);
   }

   boost::asio::awaitable<void> handle_incoming_stream(std::shared_ptr<session_state> session,
                                                       fcl::quic::stream raw) {
      try {
         auto negotiated = co_await protocol_negotiation::async_accept(std::move(raw), supported_protocols());
         if (negotiated.protocol == builtins::ping) {
            co_await handle_ping(std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::identify) {
            co_await handle_identify(std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::identify_push) {
            co_await handle_identify_push(session, std::move(negotiated.stream));
            co_return;
         }
         auto handler = handler_for(negotiated.protocol);
         if (!handler) {
            increment_protocol_rejected();
            throw_p2p_error(error_kind::unsupported_protocol, "unsupported negotiated P2P protocol");
         }
         increment_protocol_accepted();
         co_await (*handler)(node::incoming_protocol_stream{
             .session = session->info,
             .protocol = negotiated.protocol,
             .stream = std::move(negotiated.stream),
         });
      } catch (...) {
         increment_protocol_rejected();
      }
   }

   boost::asio::awaitable<void> handle_ping(fcl::p2p::stream stream) {
      if (!begin_ping_stream()) {
         throw_p2p_error(error_kind::backpressure_rejected, "libp2p ping inbound stream limit reached");
      }
      try {
         while (true) {
            auto payload = co_await stream.async_read();
            if (payload.size() != 32) {
               throw_p2p_error(error_kind::protocol_error, "libp2p ping payload must be 32 bytes");
            }
            co_await stream.async_write(payload);
         }
      } catch (const fcl::quic::quic_error& error) {
         finish_ping_stream();
         if (!is_orderly_stream_close(error)) {
            throw;
         }
         co_return;
      } catch (...) {
         finish_ping_stream();
         throw;
      }
      finish_ping_stream();
   }

   boost::asio::awaitable<void> handle_identify(fcl::p2p::stream stream) {
      auto encoded = identify::encode(local_identify_document());
      co_await stream.async_write(encoded);
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_identify_push(std::shared_ptr<session_state> session, fcl::p2p::stream stream) {
      auto payload = co_await stream.async_read();
      learn_from_identify(session->info.remote_peer, identify::decode(payload));
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_protocol_open(std::shared_ptr<session_state> session,
                                                     fcl::quic::framed_stream framed, control_message request) {
      if (request.protocol == builtins::control) {
         co_await control_codec::async_write(framed,
                                      control_message{
                                          .kind = control_message::type::protocol_accept,
                                          .request_id = request.request_id,
                                          .peer = local,
                                          .protocol = request.protocol,
                                      },
                                      codec_for(options));
         co_return;
      }
      auto handler = handler_for(request.protocol);
      if (!handler) {
         increment_protocol_rejected();
         co_await control_codec::async_write(
             framed, make_reject(control_message::type::protocol_reject, request.request_id, "unsupported P2P protocol"),
             codec_for(options));
         co_return;
      }
      co_await control_codec::async_write(framed,
                                   control_message{
                                       .kind = control_message::type::protocol_accept,
                                       .request_id = request.request_id,
                                       .peer = local,
                                       .protocol = request.protocol,
                                   },
                                   codec_for(options));
      increment_protocol_accepted();
      co_await (*handler)(node::incoming_protocol_stream{
          .session = session->info,
          .protocol = request.protocol,
          .stream = fcl::p2p::stream{std::move(framed)},
      });
   }

   boost::asio::awaitable<void> handle_peer_exchange(fcl::quic::framed_stream framed, std::uint64_t request_id) {
      auto endpoints = std::vector<control_message::endpoint_record>{};
      const auto snapshot = store.snapshot();
      for (const auto& record : snapshot) {
         for (const auto& endpoint : record.endpoints) {
            endpoints.push_back(control_message::endpoint_record{
                .peer = record.peer,
                .endpoint = endpoint.endpoint,
                .capabilities = record.capabilities,
            });
            if (endpoints.size() >= options.limits.max_peer_exchange_records) {
               break;
            }
         }
         if (endpoints.size() >= options.limits.max_peer_exchange_records) {
            break;
         }
      }
      increment_peer_exchange();
      co_await control_codec::async_write(framed,
                                   control_message{
                                       .kind = control_message::type::peer_exchange_response,
                                       .request_id = request_id,
                                       .peer = local,
                                       .endpoints = std::move(endpoints),
                                   },
                                   codec_for(options));
   }

   [[nodiscard]] std::optional<fcl::quic::endpoint> first_endpoint(const control_message& message) const {
      if (message.endpoints.empty()) {
         return std::nullopt;
      }
      return message.endpoints.front().endpoint;
   }

   boost::asio::awaitable<void> handle_reachability_probe(fcl::quic::framed_stream framed, control_message request) {
      auto state = reachability_state::private_network;
      auto observed = first_endpoint(request);
      if (observed && valid_peer_id(request.peer)) {
         try {
            auto session = co_await connect_direct(*observed, node::connect_options{
                                                                  .expected_peer = request.peer,
                                                                  .allow_relay = false,
                                                                  .timeout = std::chrono::milliseconds{1'500},
                                                              });
            session->closed = true;
            forget_session(request.peer);
            try {
               co_await session->connection.async_close();
            } catch (...) {
               session->connection.cancel();
            }
            state = reachability_state::publicly_reachable;
         } catch (const p2p_error& error) {
            state = error.kind() == error_kind::peer_verification_failed ? reachability_state::blocked
                                                                         : reachability_state::private_network;
         } catch (...) {
            state = reachability_state::private_network;
         }
      }
      increment_reachability_probe(state);
      auto endpoints = std::vector<control_message::endpoint_record>{};
      if (observed) {
         endpoints.push_back(control_message::endpoint_record{
             .peer = request.peer,
             .endpoint = *observed,
             .capabilities = request.capabilities,
         });
      }
      co_await control_codec::async_write(framed,
                                   control_message{
                                       .kind = control_message::type::reachability_result,
                                       .request_id = request.request_id,
                                       .peer = local,
                                       .reachability = state,
                                       .endpoints = std::move(endpoints),
                                   },
                                   codec_for(options));
   }

   boost::asio::awaitable<void> handle_relay_reserve(std::shared_ptr<session_state> session,
                                                     fcl::quic::framed_stream framed, control_message request) {
      if (!options.capabilities.has(capabilities::relay) ||
          !options.capabilities.has(capabilities::relay_reservation)) {
         co_await control_codec::async_write(
             framed,
             make_reject(control_message::type::relay_reject, request.request_id, "relay reservation capability is disabled"),
             codec_for(options));
         co_return;
      }
      auto reservation = remember_inbound_relay_reservation(session->info.remote_peer, request);
      if (!reservation) {
         co_await control_codec::async_write(
             framed, make_reject(control_message::type::relay_reject, request.request_id, "relay reservation limit reached"),
             codec_for(options));
         co_return;
      }
      const auto ttl = std::chrono::duration_cast<std::chrono::milliseconds>(reservation->expires_at -
                                                                             std::chrono::steady_clock::now());
      co_await control_codec::async_write(framed,
                                   control_message{
                                       .kind = control_message::type::relay_reserved,
                                       .request_id = request.request_id,
                                       .peer = local,
                                       .reservation_id = reservation->id,
                                       .ttl_ms = static_cast<std::uint64_t>(std::max<std::int64_t>(1, ttl.count())),
                                       .max_streams = static_cast<std::uint64_t>(reservation->max_streams),
                                       .max_bytes = reservation->max_bytes,
                                       .max_queued_bytes = static_cast<std::uint64_t>(reservation->max_queued_bytes),
                                   },
                                   codec_for(options));
   }

   boost::asio::awaitable<void> handle_relay_cancel(std::shared_ptr<session_state> session,
                                                    fcl::quic::framed_stream framed, control_message request) {
      (void)cancel_inbound_relay_reservation(session->info.remote_peer, request.reservation_id);
      co_await control_codec::async_write(
          framed, control_message{.kind = control_message::type::relay_reserved, .request_id = request.request_id, .peer = local},
          codec_for(options));
   }

   boost::asio::awaitable<void> handle_hole_punch_prepare(std::shared_ptr<session_state>,
                                                          fcl::quic::framed_stream framed, control_message request) {
      auto endpoints = std::vector<control_message::endpoint_record>{};
      if (auto endpoint = local_endpoint_for_control()) {
         endpoints.push_back(control_message::endpoint_record{
             .peer = local,
             .endpoint = *endpoint,
             .capabilities = options.capabilities,
         });
      }
      co_await control_codec::async_write(framed,
                                   control_message{
                                       .kind = control_message::type::hole_punch_sync,
                                       .request_id = request.request_id,
                                       .peer = local,
                                       .hole_punch = hole_punch_status::synced,
                                       .endpoints = std::move(endpoints),
                                   },
                                   codec_for(options));
      if (auto remote = first_endpoint(request); remote && valid_peer_id(request.peer)) {
         try {
            (void)co_await connect_direct(*remote, node::connect_options{
                                                       .expected_peer = request.peer,
                                                       .allow_relay = false,
                                                       .timeout = std::chrono::milliseconds{1'500},
                                                   });
         } catch (...) {
            record_hole_punch_result(hole_punch_status::failed);
         }
      }
   }

   boost::asio::awaitable<void> handle_relay_open(std::shared_ptr<session_state> session,
                                                  fcl::quic::framed_stream requester, control_message request) {
      if (!options.capabilities.has(capabilities::relay)) {
         co_await control_codec::async_write(
             requester, make_reject(control_message::type::relay_reject, request.request_id, "relay capability is disabled"),
             codec_for(options));
         co_return;
      }
      if (!begin_relay(session->info.remote_peer)) {
         co_await control_codec::async_write(
             requester,
             make_reject(control_message::type::relay_reject, request.request_id, "relay reservation or relay limit reached"),
             codec_for(options));
         co_return;
      }

      auto target = std::optional<fcl::p2p::stream>{};
      auto relay_error = std::string{};
      try {
         target.emplace(co_await open_protocol_direct(request.peer, request.protocol, node::open_options{}.timeout));
      } catch (const std::exception& error) {
         relay_error = error.what();
      }
      if (!target) {
         finish_relay(session->info.remote_peer);
         co_await control_codec::async_write(requester,
                                      make_reject(control_message::type::relay_reject, request.request_id,
                                                  relay_error.empty() ? "relay target open failed" : relay_error),
                                      codec_for(options));
         co_return;
      }

      try {
         co_await control_codec::async_write(requester,
                                      control_message{
                                          .kind = control_message::type::relay_accept,
                                          .request_id = request.request_id,
                                          .peer = local,
                                          .protocol = request.protocol,
                                      },
                                      codec_for(options));
         launch_relay_pumps(session->info.remote_peer, fcl::p2p::stream{std::move(requester)}, std::move(*target));
      } catch (const std::exception&) {
         finish_relay(session->info.remote_peer);
      }
   }

   void launch_relay_pumps(peer_id owner, fcl::p2p::stream left, fcl::p2p::stream right) {
      auto self = shared_from_this();
      struct relay_pair {
         relay_pair(peer_id owner_value, fcl::p2p::stream left_value, fcl::p2p::stream right_value)
             : owner(std::move(owner_value)), left(std::move(left_value)), right(std::move(right_value)) {}

         peer_id owner;
         fcl::p2p::stream left;
         fcl::p2p::stream right;
         std::mutex mutex;
         std::uint32_t finished = 0;
      };
      auto pair = std::make_shared<relay_pair>(std::move(owner), std::move(left), std::move(right));
      auto finish = [self, pair] {
         auto lock = std::scoped_lock{pair->mutex};
         ++pair->finished;
         if (pair->finished == 2) {
            self->finish_relay(pair->owner);
         }
      };
      asio::co_spawn(
          runtime.context(),
          [self, pair, finish]() -> asio::awaitable<void> {
             try {
                while (true) {
                   auto frame = co_await pair->left.async_read_frame();
                   if (!self->add_relay_bytes(pair->owner, frame.size())) {
                      self->record_relay_failure();
                      break;
                   }
                   co_await pair->right.async_write_frame(frame);
                }
             } catch (const fcl::quic::quic_error& error) {
                if (!is_orderly_stream_close(error)) {
                   self->record_relay_failure();
                }
             } catch (...) {
                self->record_relay_failure();
             }
             try {
                co_await pair->right.async_close();
             } catch (...) {
                // Relay cleanup is best-effort after either side closes or fails.
             }
             finish();
          },
          asio::detached);
      asio::co_spawn(
          runtime.context(),
          [self, pair, finish]() -> asio::awaitable<void> {
             try {
                while (true) {
                   auto frame = co_await pair->right.async_read_frame();
                   if (!self->add_relay_bytes(pair->owner, frame.size())) {
                      self->record_relay_failure();
                      break;
                   }
                   co_await pair->left.async_write_frame(frame);
                }
             } catch (const fcl::quic::quic_error& error) {
                if (!is_orderly_stream_close(error)) {
                   self->record_relay_failure();
                }
             } catch (...) {
                self->record_relay_failure();
             }
             try {
                co_await pair->left.async_close();
             } catch (...) {
                // Relay cleanup is best-effort after either side closes or fails.
             }
             finish();
          },
          asio::detached);
   }

   boost::asio::awaitable<hole_punch_status> attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer,
                                                                std::chrono::milliseconds timeout) {
      validate_operation_timeout(timeout, "P2P hole punch timeout");
      if (session_for(peer)) {
         co_return hole_punch_status::succeeded;
      }
      if (!relay_peer) {
         const auto record = store.find(peer);
         if (record) {
            for (const auto& endpoint : record->endpoints) {
               if (endpoint.relay_peer) {
                  relay_peer = endpoint.relay_peer;
                  break;
               }
            }
         }
      }
      if (!relay_peer) {
         throw_p2p_error(error_kind::relay_not_available, "P2P hole punching requires a relay peer");
      }
      const auto local_endpoint = local_endpoint_for_control();
      if (!local_endpoint) {
         record_hole_punch_result(hole_punch_status::failed);
         co_return hole_punch_status::failed;
      }
      try {
         auto control = co_await open_protocol_via_relay(peer, builtins::control, *relay_peer, timeout);
         const auto request_id = next_request_id();
         co_await control_codec::async_write(control,
                                      control_message{
                                          .kind = control_message::type::hole_punch_prepare,
                                          .request_id = request_id,
                                          .peer = local,
                                          .hole_punch = hole_punch_status::prepared,
                                          .endpoints = std::vector<control_message::endpoint_record>{control_message::endpoint_record{
                                              .peer = local,
                                              .endpoint = *local_endpoint,
                                              .capabilities = options.capabilities,
                                          }},
                                      },
                                      codec_for(options));
         auto response = co_await control_codec::async_read(control, codec_for(options));
         if (response.kind != control_message::type::hole_punch_sync || response.endpoints.empty()) {
            record_hole_punch_result(hole_punch_status::failed);
            co_return hole_punch_status::failed;
         }
         auto connected = false;
         try {
            (void)co_await connect_direct(response.endpoints.front().endpoint, node::connect_options{
                                                                                   .expected_peer = peer,
                                                                                   .allow_relay = false,
                                                                                   .timeout = timeout,
                                                                               });
            connected = true;
         } catch (...) {
            record_direct_failure(peer);
         }
         if (connected) {
            co_await control_codec::async_write(control,
                                         control_message{.kind = control_message::type::hole_punch_result,
                                                     .request_id = request_id,
                                                     .peer = local,
                                                     .hole_punch = hole_punch_status::succeeded},
                                         codec_for(options));
            record_hole_punch_result(hole_punch_status::succeeded);
            co_return hole_punch_status::succeeded;
         }
         co_await control_codec::async_write(control,
                                      control_message{.kind = control_message::type::hole_punch_result,
                                                  .request_id = request_id,
                                                  .peer = local,
                                                  .hole_punch = hole_punch_status::failed},
                                      codec_for(options));
      } catch (...) {
         // The relay-assisted hole-punch path is still legacy-internal; surface failure through status.
      }
      record_hole_punch_result(hole_punch_status::failed);
      co_return hole_punch_status::failed;
   }
};

node::node(fcl::asio::runtime& runtime, node::options options) {
   validate(options);
   impl_ = std::make_shared<impl>(runtime, std::move(options));
}

node::~node() = default;
node::node(node&&) noexcept = default;
node& node::operator=(node&&) noexcept = default;

const peer_id& node::local_peer() const noexcept {
   return impl_->local;
}

std::optional<fcl::quic::endpoint> node::local_endpoint() const {
   auto lock = std::scoped_lock{impl_->mutex};
   if (!impl_->listener) {
      return std::nullopt;
   }
   return impl_->listener->local_endpoint();
}

node::metrics_snapshot node::metrics() const {
   auto lock = std::scoped_lock{impl_->mutex};
   impl_->cleanup_expired_relay_reservations_locked();
   auto out = impl_->metrics_value;
   out.active_sessions = impl_->sessions.size();
   out.active_relay_reservations = impl_->inbound_relay_reservations.size();
   out.stopped = impl_->stopped;
   return out;
}

peer_store& node::peers() noexcept {
   return impl_->store;
}

const peer_store& node::peers() const noexcept {
   return impl_->store;
}

void node::register_protocol_handler(protocol_id protocol, node::protocol_handler handler) {
   if (protocol.value.empty() || !handler) {
      throw_p2p_error(error_kind::invalid_options, "P2P protocol handler requires protocol id and handler");
   }
   auto lock = std::scoped_lock{impl_->mutex};
   if (impl_->handlers.size() >= impl_->options.limits.max_protocol_handlers) {
      throw_p2p_error(error_kind::backpressure_rejected, "P2P max protocol handlers reached");
   }
   const auto [_, inserted] = impl_->handlers.emplace(std::move(protocol), std::move(handler));
   if (!inserted) {
      throw_p2p_error(error_kind::duplicate_protocol, "duplicate P2P protocol handler");
   }
}

boost::asio::awaitable<void> node::async_listen(fcl::quic::endpoint endpoint) {
   auto self = impl_;
   {
      auto lock = std::scoped_lock{self->mutex};
      if (self->stopped) {
         throw_p2p_error(error_kind::closed, "P2P node is stopped");
      }
      if (self->listener) {
         throw_p2p_error(error_kind::invalid_options, "P2P node is already listening");
      }
      self->listener =
          std::make_unique<fcl::quic::listener>(self->runtime, std::move(endpoint), self->quic_server_options());
   }
   self->launch_accept_loop();
   co_return;
}

boost::asio::awaitable<node::session_info> node::async_connect(fcl::quic::endpoint endpoint) {
   return async_connect(std::move(endpoint), connect_options{});
}

boost::asio::awaitable<node::session_info> node::async_connect(fcl::quic::endpoint endpoint,
                                                               node::connect_options options) {
   validate_operation_timeout(options.timeout, "P2P connect timeout");
   auto self = impl_;
   auto session = co_await self->connect_direct(std::move(endpoint), std::move(options));
   co_return session->info;
}

boost::asio::awaitable<void> node::async_request_peer_exchange(peer_id peer) {
   auto self = impl_;
   co_await self->request_peer_exchange(peer);
}

boost::asio::awaitable<reachability_state> node::async_probe_reachability(peer_id observer) {
   auto self = impl_;
   auto endpoint = self->local_endpoint_for_control();
   if (!endpoint) {
      co_return reachability_state::private_network;
   }
   auto session = co_await self->ensure_direct_session(observer);
   auto framed = fcl::quic::framed_stream{
       co_await session->connection.async_open_stream(),
       frame_codec_for(self->options),
   };
   const auto request_id = self->next_request_id();
   co_await control_codec::async_write(framed,
                                control_message{
                                    .kind = control_message::type::reachability_probe,
                                    .request_id = request_id,
                                    .peer = self->local,
                                    .capabilities = self->options.capabilities,
                                    .endpoints = std::vector<control_message::endpoint_record>{control_message::endpoint_record{
                                        .peer = self->local,
                                        .endpoint = *endpoint,
                                        .capabilities = self->options.capabilities,
                                    }},
                                },
                                codec_for(self->options));
   auto response = co_await control_codec::async_read(framed, codec_for(self->options));
   if (response.kind != control_message::type::reachability_result) {
      throw_p2p_error(error_kind::protocol_error, "P2P reachability probe expected result");
   }
   self->store.mark_reachability(self->local, response.reachability,
                                 response.endpoints.empty() ? std::nullopt
                                                            : std::make_optional(response.endpoints.front().endpoint));
   co_return response.reachability;
}

boost::asio::awaitable<relay_reservation::info> node::async_reserve_relay(peer_id relay_peer) {
   return async_reserve_relay(std::move(relay_peer), relay_reservation::options{});
}

boost::asio::awaitable<relay_reservation::info> node::async_reserve_relay(peer_id relay_peer,
                                                                          relay_reservation::options options) {
   auto self = impl_;
   co_return co_await self->request_relay_reservation(relay_peer, options, node::connect_options{}.timeout);
}

boost::asio::awaitable<void> node::async_cancel_relay(peer_id relay_peer) {
   auto self = impl_;
   auto reservation = std::optional<impl::relay_reservation_state>{};
   {
      auto lock = std::scoped_lock{self->mutex};
      self->cleanup_expired_relay_reservations_locked();
      const auto it = self->outbound_relay_reservations.find(relay_peer);
      if (it == self->outbound_relay_reservations.end()) {
         co_return;
      }
      reservation = it->second;
      self->outbound_relay_reservations.erase(it);
   }
   auto session = co_await self->ensure_direct_session(relay_peer);
   auto framed = fcl::quic::framed_stream{
       co_await session->connection.async_open_stream(),
       frame_codec_for(self->options),
   };
   const auto request_id = self->next_request_id();
   co_await control_codec::async_write(framed,
                                control_message{
                                    .kind = control_message::type::relay_cancel,
                                    .request_id = request_id,
                                    .peer = self->local,
                                    .reservation_id = reservation->id,
                                },
                                codec_for(self->options));
   (void)co_await control_codec::async_read(framed, codec_for(self->options));
}

boost::asio::awaitable<std::chrono::milliseconds> node::async_ping(peer_id peer) {
   co_return co_await async_ping(std::move(peer), open_options{});
}

boost::asio::awaitable<std::chrono::milliseconds> node::async_ping(peer_id peer, open_options options) {
   auto started = std::chrono::steady_clock::now();
   auto stream = co_await async_open_protocol_stream(std::move(peer), builtins::ping, std::move(options));
   const auto payload = fcl::crypto::random_bytes(32);
   co_await stream.async_write(payload);
   const auto reply = co_await stream.async_read();
   if (reply != payload) {
      throw_p2p_error(error_kind::protocol_error, "libp2p ping payload mismatch");
   }
   co_await stream.async_close();
   co_return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
}

boost::asio::awaitable<hole_punch_status>
node::async_attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer, std::chrono::milliseconds timeout) {
   validate_operation_timeout(timeout, "P2P hole punch timeout");
   auto self = impl_;
   if (self->session_for(peer)) {
      co_return hole_punch_status::succeeded;
   }
   if (!relay_peer) {
      const auto record = self->store.find(peer);
      if (record) {
         for (const auto& endpoint : record->endpoints) {
            if (endpoint.relay_peer) {
               relay_peer = endpoint.relay_peer;
               break;
            }
         }
      }
   }
   if (!relay_peer) {
      throw_p2p_error(error_kind::relay_not_available, "P2P hole punching requires a relay peer");
   }
   const auto local_endpoint = self->local_endpoint_for_control();
   if (!local_endpoint) {
      self->record_hole_punch_result(hole_punch_status::failed);
      co_return hole_punch_status::failed;
   }
   try {
      auto control = co_await self->open_protocol_via_relay(peer, builtins::control, *relay_peer, timeout);
      const auto request_id = self->next_request_id();
      co_await control_codec::async_write(control,
                                   control_message{
                                       .kind = control_message::type::hole_punch_prepare,
                                       .request_id = request_id,
                                       .peer = self->local,
                                       .hole_punch = hole_punch_status::prepared,
                                       .endpoints = std::vector<control_message::endpoint_record>{control_message::endpoint_record{
                                           .peer = self->local,
                                           .endpoint = *local_endpoint,
                                           .capabilities = self->options.capabilities,
                                       }},
                                   },
                                   codec_for(self->options));
      auto response = co_await control_codec::async_read(control, codec_for(self->options));
      if (response.kind != control_message::type::hole_punch_sync || response.endpoints.empty()) {
         self->record_hole_punch_result(hole_punch_status::failed);
         co_return hole_punch_status::failed;
      }
      auto connected = false;
         try {
            (void)co_await self->connect_direct(response.endpoints.front().endpoint, node::connect_options{
                                                                                      .expected_peer = peer,
                                                                                      .allow_relay = false,
                                                                                      .timeout = timeout,
                                                                                  });
            connected = true;
         } catch (...) {
            self->record_direct_failure(peer);
         }
      if (connected) {
         co_await control_codec::async_write(control,
                                      control_message{.kind = control_message::type::hole_punch_result,
                                                  .request_id = request_id,
                                                  .peer = self->local,
                                                  .hole_punch = hole_punch_status::succeeded},
                                      codec_for(self->options));
         self->record_hole_punch_result(hole_punch_status::succeeded);
         co_return hole_punch_status::succeeded;
      }
      co_await control_codec::async_write(control,
                                   control_message{.kind = control_message::type::hole_punch_result,
                                               .request_id = request_id,
                                               .peer = self->local,
                                               .hole_punch = hole_punch_status::failed},
                                   codec_for(self->options));
   } catch (...) {
      // The public operation returns a failed status; legacy relay errors stay local to metrics.
   }
   self->record_hole_punch_result(hole_punch_status::failed);
   co_return hole_punch_status::failed;
}

boost::asio::awaitable<fcl::p2p::stream> node::async_open_protocol_stream(peer_id peer, protocol_id protocol) {
   return async_open_protocol_stream(std::move(peer), std::move(protocol), open_options{});
}

boost::asio::awaitable<fcl::p2p::stream> node::async_open_protocol_stream(peer_id peer, protocol_id protocol,
                                                                          node::open_options options) {
   validate_operation_timeout(options.timeout, "P2P protocol open timeout");
   validate_operation_timeout(options.direct_attempt_timeout, "P2P direct attempt timeout");
   validate_operation_timeout(options.relay_attempt_timeout, "P2P relay attempt timeout");
   if (options.max_direct_endpoints == 0 || options.max_relay_candidates == 0) {
      throw_p2p_error(error_kind::invalid_options, "P2P path attempt limits must be positive");
   }
   auto self = impl_;
   const auto started = std::chrono::steady_clock::now();
   auto last_kind = std::optional<error_kind>{};
   auto last_message = std::string{};
   try {
      co_return co_await self->open_protocol_direct(peer, protocol, options.timeout, options.max_direct_endpoints,
                                                    options.direct_attempt_timeout);
   } catch (const p2p_error& error) {
      last_kind = error.kind();
      last_message = error.what();
      if (error.kind() == error_kind::unsupported_protocol || error.kind() == error_kind::protocol_error ||
          error.kind() == error_kind::codec_error) {
         throw;
      }
      try {
         (void)remaining_timeout(started, options.timeout, "P2P protocol open");
      } catch (const p2p_error&) {
         throw;
      }
      if (!options.allow_relay && !(options.allow_hole_punch && options.relay_peer)) {
         throw;
      }
   }

   auto relay_candidates = std::vector<peer_id>{};
   if (options.relay_peer) {
      relay_candidates.push_back(*options.relay_peer);
   } else if (options.allow_relay || options.allow_hole_punch) {
      const auto snapshot = self->store.snapshot();
      auto relay_records = std::vector<peer_store::record>{};
      for (const auto& record : snapshot) {
         if (record.peer == peer) {
            continue;
         }
         if (!record.capabilities.has(capabilities::relay) ||
             !record.capabilities.has(capabilities::relay_reservation)) {
            continue;
         }
         relay_records.push_back(record);
      }
      std::stable_sort(relay_records.begin(), relay_records.end(),
                       [](const auto& left, const auto& right) { return left.score > right.score; });
      for (const auto& record : relay_records) {
         if (relay_candidates.size() >= options.max_relay_candidates) {
            break;
         }
         relay_candidates.push_back(record.peer);
      }
   }

   if (options.allow_hole_punch) {
      for (const auto& relay_peer : relay_candidates) {
         const auto remaining = remaining_timeout(started, options.timeout, "P2P hole punch");
         const auto per_attempt = attempt_timeout(remaining, options.relay_attempt_timeout, "P2P hole punch attempt");
         try {
            const auto status = co_await self->attempt_hole_punch(peer, relay_peer, per_attempt);
            if (status == hole_punch_status::succeeded) {
               co_return co_await self->open_protocol_direct(
                   peer, protocol, remaining_timeout(started, options.timeout, "P2P protocol open after hole punch"),
                   options.max_direct_endpoints, options.direct_attempt_timeout);
            }
         } catch (const p2p_error& error) {
            last_kind = error.kind();
            last_message = error.what();
         }
      }
   }

   if (!options.allow_relay) {
      if (last_kind) {
         throw_p2p_error(*last_kind, last_message);
      }
      throw_p2p_error(error_kind::relay_not_available, "P2P relay fallback is disabled");
   }

   if (relay_candidates.empty()) {
      throw_p2p_error(error_kind::relay_not_available, "P2P path manager found no reserved relay candidate");
   }
   self->record_direct_failure(peer);
   for (const auto& relay_peer : relay_candidates) {
      const auto remaining = remaining_timeout(started, options.timeout, "P2P protocol open");
      const auto per_attempt = attempt_timeout(remaining, options.relay_attempt_timeout, "P2P relay path attempt");
      try {
         co_return fcl::p2p::stream{co_await self->open_protocol_via_relay(peer, protocol, relay_peer, per_attempt)};
      } catch (const p2p_error& error) {
         last_kind = error.kind();
         last_message = error.what();
      }
   }
   if (last_kind) {
      throw_p2p_error(*last_kind, last_message);
   }
   throw_p2p_error(error_kind::relay_not_available, "P2P path manager exhausted relay candidates");
}

boost::asio::awaitable<void> node::async_stop() {
   auto self = impl_;
   std::vector<std::shared_ptr<impl::session_state>> sessions;
   {
      auto lock = std::scoped_lock{self->mutex};
      if (self->stopped) {
         co_return;
      }
      self->stopped = true;
      if (self->listener) {
         self->listener->stop();
      }
      for (auto& [_, session] : self->sessions) {
         session->closed = true;
         sessions.push_back(session);
      }
      self->sessions.clear();
      self->inbound_relay_reservations.clear();
      self->outbound_relay_reservations.clear();
      self->metrics_value.active_sessions = 0;
      self->metrics_value.active_relay_reservations = 0;
      self->metrics_value.stopped = true;
   }
   for (auto& session : sessions) {
      try {
         co_await session->connection.async_close();
      } catch (...) {
         session->connection.cancel();
      }
   }
}

void node::stop() {
   {
      auto lock = std::scoped_lock{impl_->mutex};
      if (impl_->stopped) {
         return;
      }
      impl_->stopped = true;
      if (impl_->listener) {
         impl_->listener->stop();
      }
      for (auto& [_, session] : impl_->sessions) {
         session->closed = true;
         session->connection.cancel();
      }
      impl_->sessions.clear();
      impl_->inbound_relay_reservations.clear();
      impl_->outbound_relay_reservations.clear();
      impl_->metrics_value.active_sessions = 0;
      impl_->metrics_value.active_relay_reservations = 0;
      impl_->metrics_value.stopped = true;
   }
}

} // namespace fcl::p2p
