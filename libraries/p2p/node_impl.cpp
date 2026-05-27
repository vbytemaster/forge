module;

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

module fcl.p2p.node;

import fcl.crypto.chacha20_poly1305;
import fcl.crypto.der;
import fcl.crypto.ed25519;
import fcl.crypto.hmac;
import fcl.crypto.pem;
import fcl.crypto.asymmetric;
import fcl.p2p.dht;
import fcl.p2p.discovery;
import fcl.p2p.endpoint;
import fcl.p2p.envelope;
import fcl.p2p.hole_punch;
import fcl.p2p.identify;
import fcl.p2p.exceptions;
import fcl.p2p.message;
import fcl.p2p.negotiation;
import fcl.p2p.reachability;
import fcl.p2p.rendezvous;
import fcl.p2p.resource_manager;
import fcl.p2p.scoring;
import fcl.p2p.stream;
import fcl.crypto.random;
import fcl.crypto.rsa;
import fcl.crypto.sha256;
import fcl.crypto.x25519;
import fcl.multiformats.types;
import fcl.multiformats.varint;
import fcl.multiformats.exceptions;
import fcl.quic.connection;
import fcl.quic.connector;
import fcl.quic.exceptions;
import fcl.quic.framed_stream;
import fcl.quic.listener;
import fcl.quic.options;
import fcl.quic.security;

#include "node_impl.hpp"

namespace fcl::p2p {

namespace asio = boost::asio;

[[nodiscard]] exceptions::code map_quic_error(fcl::quic::exceptions::code kind) noexcept {
   using quic_kind = fcl::quic::exceptions::code;
   switch (kind) {
   case quic_kind::invalid_endpoint:
   case quic_kind::invalid_options:
      return exceptions::code::invalid_options;
   case quic_kind::connect_timeout:
   case quic_kind::handshake_timeout:
   case quic_kind::idle_timeout:
      return exceptions::code::timeout;
   case quic_kind::peer_verification_failed:
   case quic_kind::alpn_mismatch:
   case quic_kind::tls_failed:
      return exceptions::code::peer_verification_failed;
   case quic_kind::frame_too_large:
   case quic_kind::malformed_frame:
      return exceptions::code::codec_error;
   case quic_kind::backpressure_rejected:
      return exceptions::code::backpressure_rejected;
   case quic_kind::connection_closed:
   case quic_kind::stream_closed:
   case quic_kind::stream_reset:
      return exceptions::code::closed;
   case quic_kind::canceled:
      return exceptions::code::canceled;
   case quic_kind::dependency_unavailable:
   case quic_kind::internal:
   case quic_kind::unsupported:
      return exceptions::code::internal;
   }
   return exceptions::code::internal;
}

[[nodiscard]] exceptions::code p2p_code(const fcl::exception::base& error) {
   const auto code = exceptions::code_of(error);
   if (!code) {
      throw;
   }
   return *code;
}

[[nodiscard]] fcl::quic::exceptions::code quic_code(const fcl::exception::base& error) {
   const auto code = fcl::quic::exceptions::code_of(error);
   if (!code) {
      throw;
   }
   return *code;
}

[[noreturn]] void rethrow_quic_as_p2p(const fcl::exception::base& error) {
   exceptions::raise(map_quic_error(quic_code(error)), error.what());
}

[[nodiscard]] bool is_orderly_stream_close(const fcl::exception::base& error) noexcept {
   return fcl::quic::exceptions::is(error, fcl::quic::exceptions::code::stream_closed);
}

[[nodiscard]] std::uint64_t random_nonce() {
   const auto bytes = fcl::crypto::random_bytes(8);
   auto out = std::uint64_t{};
   for (auto byte : bytes) {
      out = (out << 8U) | byte;
   }
   return out == 0 ? 1 : out;
}

boost::asio::awaitable<std::vector<std::uint8_t>>
async_read_length_delimited(fcl::p2p::stream& stream, std::vector<std::uint8_t>& buffer, std::size_t max_payload_size) {
   while (true) {
      try {
         const auto decoded = fcl::multiformats::varint_decode(buffer);
         if (decoded.value > max_payload_size) {
            exceptions::raise(exceptions::code::codec_error, "libp2p protobuf message exceeds max size");
         }
         const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
         if (buffer.size() >= total) {
            auto out = std::vector<std::uint8_t>{buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total)};
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total));
            co_return out;
         }
      } catch (const fcl::multiformats::exceptions::invalid_format& error) {
         if (std::string_view{error.what()}.find("unterminated") == std::string_view::npos) {
            exceptions::raise(exceptions::code::codec_error, error.what());
         }
      }
      auto chunk = co_await stream.async_read();
      buffer.insert(buffer.end(), chunk.begin(), chunk.end());
   }
}

[[nodiscard]] std::vector<std::uint8_t> wrap_length_delimited(std::span<const std::uint8_t> payload) {
   auto out = fcl::multiformats::varint_encode(payload.size());
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> unwrap_length_delimited(std::span<const std::uint8_t> bytes,
                                                                std::size_t max_payload_size) {
   auto decoded = fcl::multiformats::decoded_varint{};
   try {
      decoded = fcl::multiformats::varint_decode(bytes);
   } catch (const fcl::multiformats::exceptions::invalid_format& error) {
      exceptions::raise(exceptions::code::codec_error, error.what());
   }
   if (decoded.value > max_payload_size) {
      exceptions::raise(exceptions::code::codec_error, "libp2p protobuf message exceeds max size");
   }
   const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
   if (total != bytes.size()) {
      exceptions::raise(exceptions::code::codec_error, "libp2p protobuf message length mismatch");
   }
   return {bytes.begin() + static_cast<std::ptrdiff_t>(decoded.size), bytes.end()};
}

[[nodiscard]] peer_exchange_codec::options codec_for(const node::options& options) noexcept {
   return peer_exchange_codec::options{
       .max_message_size = static_cast<std::uint32_t>(options.limits.max_peer_exchange_message_size),
       .max_endpoint_records = static_cast<std::uint32_t>(options.limits.max_peer_exchange_records),
   };
}

[[nodiscard]] fcl::quic::frame_codec_options frame_codec_for(const node::options& options) noexcept {
   return fcl::quic::frame_codec_options{
       .max_frame_size = static_cast<std::uint32_t>(options.transport_limits.max_frame_size),
   };
}

void validate_operation_timeout(std::chrono::milliseconds timeout, std::string_view name) {
   if (timeout.count() <= 0) {
      exceptions::raise(exceptions::code::invalid_options, std::string{name} + " must be positive");
   }
}

[[nodiscard]] std::chrono::milliseconds remaining_timeout(std::chrono::steady_clock::time_point started,
                                                          std::chrono::milliseconds timeout,
                                                          std::string_view operation) {
   validate_operation_timeout(timeout, operation);
   const auto elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
   if (elapsed >= timeout) {
      exceptions::raise(exceptions::code::timeout, std::string{operation} + " timed out");
   }
   return timeout - elapsed;
}

[[nodiscard]] std::chrono::milliseconds
attempt_timeout(std::chrono::milliseconds remaining, std::chrono::milliseconds configured, std::string_view operation) {
   validate_operation_timeout(remaining, operation);
   validate_operation_timeout(configured, operation);
   return std::min(remaining, configured);
}


[[noreturn]] void throw_operation_timeout(std::string_view operation) {
   exceptions::raise(exceptions::code::timeout, std::string{operation} + " timed out");
}

void validate(const node::options& options) {
   if (!options.allow_insecure_test_mode && (options.certificate_pem.empty() || options.private_key_pem.empty())) {
      exceptions::raise(exceptions::code::invalid_options, "production P2P node requires mTLS certificate and private key");
   }
   if (options.certificate_pem.empty() != options.private_key_pem.empty()) {
      exceptions::raise(exceptions::code::invalid_options, "P2P certificate and private key must be provided together");
   }
   if (options.explicit_peer_id && !valid_peer_id(*options.explicit_peer_id)) {
      exceptions::raise(exceptions::code::invalid_options, "invalid explicit P2P peer id");
   }
   if (options.allow_insecure_test_mode && options.certificate_pem.empty() && !options.explicit_peer_id) {
      exceptions::raise(exceptions::code::invalid_options,
                      "insecure P2P test node without certificate requires explicit peer id");
   }
   if (options.peer_store_backend && options.peer_store_path) {
      exceptions::raise(exceptions::code::invalid_options, "P2P peer store backend and path are mutually exclusive");
   }
   if (!options.allow_insecure_test_mode && !options.peer_store_backend && !options.peer_store_path) {
      exceptions::raise(exceptions::code::invalid_options, "production P2P node requires persistent peer store path");
   }
   if (options.peer_store_path && options.peer_store_path->empty()) {
      exceptions::raise(exceptions::code::invalid_options, "P2P peer store path must not be empty");
   }
   if (options.limits.max_sessions == 0 || options.limits.max_protocol_handlers == 0 ||
       options.limits.max_peer_exchange_message_size == 0 || options.limits.max_peer_exchange_records == 0 ||
       options.limits.max_peer_exchange_queue == 0 || options.limits.relay.max_active_relays == 0 ||
       options.limits.relay.max_reservations == 0 || options.limits.relay.max_streams_per_reservation == 0 ||
       options.limits.relay.max_relay_bytes == 0 || options.limits.relay.max_queued_bytes == 0 ||
       options.limits.relay.max_duration.count() <= 0 || options.limits.relay.reservation_ttl.count() <= 0 ||
       options.limits.resources.max_streams == 0 || options.limits.resources.max_streams_per_peer == 0 ||
       options.limits.resources.max_streams_per_protocol == 0 || options.limits.resources.max_relay_reservations == 0 ||
       options.limits.resources.max_relay_streams == 0 || options.limits.resources.max_relay_bytes == 0 ||
       options.limits.resources.max_queued_bytes == 0 || options.limits.resources.max_dial_attempts_per_peer == 0 ||
       options.limits.resources.max_malformed_messages_per_peer == 0 ||
       options.limits.discovery.query_timeout.count() <= 0 || options.limits.discovery.refresh_interval.count() <= 0 ||
       options.limits.discovery.max_parallel_queries == 0 || options.limits.discovery.max_results == 0 ||
       options.limits.dht.replication == 0 || options.limits.dht.alpha == 0 ||
       options.limits.dht.max_message_size == 0 || options.limits.dht.max_record_size == 0 ||
       options.limits.dht.max_closer_peers == 0 || options.limits.dht.max_provider_peers == 0 ||
       options.limits.dht.query_timeout.count() <= 0 || options.limits.dht.refresh_interval.count() <= 0 ||
       options.limits.dht.provider_record_ttl.count() <= 0 ||
       options.limits.rendezvous.default_ttl.count() <= 0 || options.limits.rendezvous.min_ttl.count() <= 0 ||
       options.limits.rendezvous.max_ttl.count() <= 0 ||
       options.limits.rendezvous.min_ttl > options.limits.rendezvous.max_ttl ||
       options.limits.rendezvous.max_namespace_size == 0 ||
       options.limits.rendezvous.max_registrations_per_peer == 0 ||
       options.limits.rendezvous.max_discover_limit == 0 ||
       options.limits.rendezvous.max_message_size == 0) {
      exceptions::raise(exceptions::code::invalid_options, "invalid P2P node limits");
   }
   if (!options.path_policy.allow_direct && !options.path_policy.allow_hole_punch && !options.path_policy.allow_relay) {
      exceptions::raise(exceptions::code::invalid_options, "P2P path policy must allow at least one path kind");
   }
   if (options.path_policy.max_direct_endpoints == 0 || options.path_policy.max_relay_candidates == 0) {
      exceptions::raise(exceptions::code::invalid_options, "P2P path policy limits must be positive");
   }
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

node::impl::impl(fcl::asio::runtime& runtime_value, node::options options_value)
    : runtime(runtime_value), options(std::move(options_value)),
      local(options.explicit_peer_id ? *options.explicit_peer_id
                                     : make_peer_id_from_certificate_pem(options.certificate_pem)),
      connector(runtime_value), store(peer_store::options{.backend = make_peer_store_backend(options)}) {}

[[nodiscard]] std::optional<fcl::quic::endpoint> node::impl::local_endpoint_for_control() const {
   auto lock = std::scoped_lock{mutex};
   if (listener) {
      return listener->local_endpoint();
   }
   if (!options.advertised_endpoints.empty()) {
      return options.advertised_endpoints.front();
   }
   return std::nullopt;
}

[[nodiscard]] fcl::quic::security_options node::impl::peer_verifier(std::optional<peer_id> expected) const {
   if (options.allow_insecure_test_mode) {
      auto security = fcl::quic::security_options{.verify_peer = true};
      security.verifier = [](const fcl::quic::peer_certificate&) { return true; };
      return security;
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

[[nodiscard]] fcl::quic::client_options node::impl::quic_client_options(std::optional<peer_id> expected) const {
   return fcl::quic::client_options{
       .alpn = "libp2p",
       .limits = options.transport_limits,
       .security = peer_verifier(std::move(expected)),
       .certificate_pem = options.certificate_pem,
       .private_key_pem = options.private_key_pem,
   };
}

[[nodiscard]] fcl::quic::client_options node::impl::quic_client_options(std::optional<peer_id> expected,
                                                            std::chrono::milliseconds timeout) const {
   auto out = quic_client_options(std::move(expected));
   out.connect_timeout = timeout;
   out.handshake_timeout = timeout;
   return out;
}

[[nodiscard]] fcl::quic::server_options node::impl::quic_server_options() const {
   return fcl::quic::server_options{
       .alpn = "libp2p",
       .limits = options.transport_limits,
       .security = peer_verifier(),
       .certificate_pem = options.certificate_pem,
       .private_key_pem = options.private_key_pem,
   };
}

[[nodiscard]] peer_id node::impl::verified_peer_id(const fcl::quic::connection& connection,
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
      exceptions::raise(exceptions::code::peer_verification_failed, "P2P session has no verified peer certificate");
   }
   const auto certificate_peer = make_peer_id_from_certificate(*certificate);
   if (expected && *expected != certificate_peer) {
      exceptions::raise(exceptions::code::peer_verification_failed, "P2P peer id does not match expected peer");
   }
   return certificate_peer;
}

void node::impl::learn_from_message(const peer_exchange_message& message) {
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

[[nodiscard]] fcl::p2p::endpoint node::impl::p2p_endpoint_for(const fcl::quic::endpoint& value) const {
   return fcl::p2p::endpoint{
       .kind = fcl::p2p::endpoint::address_kind::ip4,
       .host = value.host,
       .port = value.port,
       .peer = local,
   };
}

[[nodiscard]] identify::document node::impl::local_identify_document() const {
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
       .public_key = options.public_key,
       .listen_endpoints = std::move(endpoints),
       .protocols = supported_protocols(),
   };
}

void node::impl::learn_from_identify(const peer_id& peer, const identify::document& document) {
   auto record = store.find(peer).value_or(peer_store::record{.peer = peer});
   record.protocol_version = document.protocol_version;
   record.agent_version = document.agent_version;
   record.public_key = document.public_key;
   record.protocols = document.protocols;
   record.signed_peer_record = document.signed_peer_record;
   record.observed_endpoint = document.observed_endpoint
                                  ? std::make_optional(document.observed_endpoint->quic_endpoint())
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

void node::impl::remember_session(std::shared_ptr<node::impl::session_state> session) {
   auto lock = std::scoped_lock{mutex};
   if (sessions.size() >= options.limits.max_sessions && !sessions.contains(session->info.remote_peer)) {
      ++metrics_value.backpressure_rejections;
      exceptions::raise(exceptions::code::backpressure_rejected, "P2P max sessions reached");
   }
   sessions[session->info.remote_peer] = std::move(session);
   metrics_value.active_sessions = sessions.size();
   ++metrics_value.sessions_opened;
   ++metrics_value.handshakes_completed;
}

void node::impl::forget_session(const peer_id& peer) {
   auto lock = std::scoped_lock{mutex};
   if (sessions.erase(peer) != 0) {
      metrics_value.active_sessions = sessions.size();
      ++metrics_value.sessions_closed;
   }
}

[[nodiscard]] std::shared_ptr<node::impl::session_state> node::impl::session_for(const peer_id& peer) const {
   auto lock = std::scoped_lock{mutex};
   const auto it = sessions.find(peer);
   if (it == sessions.end()) {
      return {};
   }
   return it->second;
}

[[nodiscard]] std::optional<node::protocol_handler> node::impl::handler_for(const protocol_id& protocol) const {
   auto lock = std::scoped_lock{mutex};
   const auto it = handlers.find(protocol);
   if (it == handlers.end()) {
      return std::nullopt;
   }
   return it->second;
}

[[nodiscard]] std::vector<protocol_id> node::impl::supported_protocols() const {
   auto lock = std::scoped_lock{mutex};
   auto out = std::vector<protocol_id>{builtins::ping,
                                       builtins::identify,
                                       builtins::identify_push,
                                       builtins::autonat_v2_dial_request,
                                       builtins::autonat_v2_dial_back,
                                       builtins::autonat_v1,
                                       builtins::relay_stop,
                                       builtins::dcutr};
   if (options.capabilities.has(capabilities::relay) || options.capabilities.has(capabilities::relay_reservation)) {
      out.push_back(builtins::relay_hop);
   }
   if (options.capabilities.has(capabilities::dht) && options.limits.dht.operating_mode == dht::mode::server) {
      out.push_back(builtins::kad_dht);
   }
   if (options.capabilities.has(capabilities::rendezvous) &&
       (options.limits.rendezvous.operating_role == rendezvous::role::server ||
        options.limits.rendezvous.operating_role == rendezvous::role::client_and_server)) {
      out.push_back(builtins::rendezvous);
   }
   out.reserve(out.size() + handlers.size());
   for (const auto& [protocol, _] : handlers) {
      out.push_back(protocol);
   }
   return out;
}

void node::impl::remember_autonat_v2_nonce(const peer_id& peer, std::uint64_t nonce) {
   auto lock = std::scoped_lock{mutex};
   pending_autonat_v2_nonces[peer] = nonce;
}

void node::impl::forget_autonat_v2_nonce(const peer_id& peer) {
   auto lock = std::scoped_lock{mutex};
   pending_autonat_v2_nonces.erase(peer);
}

[[nodiscard]] bool node::impl::consume_autonat_v2_nonce(const peer_id& peer, std::uint64_t nonce) {
   auto lock = std::scoped_lock{mutex};
   const auto it = pending_autonat_v2_nonces.find(peer);
   if (it != pending_autonat_v2_nonces.end() && it->second == nonce) {
      pending_autonat_v2_nonces.erase(it);
      return true;
   }
   if (options.allow_insecure_test_mode) {
      const auto nonce_it =
          std::ranges::find_if(pending_autonat_v2_nonces, [&](const auto& item) { return item.second == nonce; });
      if (nonce_it != pending_autonat_v2_nonces.end()) {
         pending_autonat_v2_nonces.erase(nonce_it);
         return true;
      }
   }
   return false;
}

void node::impl::increment_opened_protocol() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.protocol_streams_opened;
}

void node::impl::increment_protocol_accepted() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.protocol_streams_accepted;
}

void node::impl::increment_protocol_rejected() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.protocol_rejections;
}

void node::impl::increment_peer_exchange() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.peer_exchange_messages;
}

[[nodiscard]] bool node::impl::begin_ping_stream() {
   auto lock = std::scoped_lock{mutex};
   if (active_ping_streams >= 2) {
      ++metrics_value.backpressure_rejections;
      ++metrics_value.protocol_rejections;
      return false;
   }
   ++active_ping_streams;
   return true;
}

void node::impl::finish_ping_stream() {
   auto lock = std::scoped_lock{mutex};
   if (active_ping_streams > 0) {
      --active_ping_streams;
   }
}

void node::impl::increment_reachability_check(reachability::state state) {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.reachability_checks;
   if (state == reachability::state::publicly_reachable) {
      ++metrics_value.reachability_public;
   } else if (state == reachability::state::private_network || state == reachability::state::blocked ||
              state == reachability::state::relay_only) {
      ++metrics_value.reachability_private;
   }
}

void node::impl::cleanup_expired_relay_reservations_locked() {
   const auto now = std::chrono::steady_clock::now();
   for (auto it = inbound_relay_reservations.begin(); it != inbound_relay_reservations.end();) {
      if (it->second.canceled || it->second.expires_at <= now) {
         if (metrics_value.active_relay_reservations > 0) {
            --metrics_value.active_relay_reservations;
         }
         resources.release_relay_reservation(
             resource_manager::scope{.peer = it->second.owner, .protocol = builtins::relay_hop});
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

[[nodiscard]] bool node::impl::has_outbound_relay_reservation(const peer_id& relay_peer) {
   auto lock = std::scoped_lock{mutex};
   cleanup_expired_relay_reservations_locked();
   return outbound_relay_reservations.contains(relay_peer);
}

bool node::impl::remember_outbound_relay_reservation(node::impl::relay_reservation_state reservation) {
   auto lock = std::scoped_lock{mutex};
   cleanup_expired_relay_reservations_locked();
   outbound_relay_reservations[reservation.relay_peer] = std::move(reservation);
   return true;
}

void node::impl::remember_relay_reservation_in_store(const relay::reservation::info& info) {
   auto record = store.find(info.relay_peer).value_or(peer_store::record{.peer = info.relay_peer});
   auto relay_endpoints = std::vector<fcl::quic::endpoint>{};
   relay_endpoints.reserve(info.relay_endpoints.size());
   for (const auto& endpoint : info.relay_endpoints) {
      relay_endpoints.push_back(endpoint.quic_endpoint());
   }
   auto reservation = peer_store::relay_record{
       .relay = info.relay_peer,
       .reservation_id = info.id,
       .expires_at = std::chrono::system_clock::time_point{info.expires_at},
       .endpoints = std::move(relay_endpoints),
       .voucher = info.voucher ? info.voucher->encode() : std::vector<std::uint8_t>{},
   };
   const auto current = std::ranges::find_if(record.relay_reservations,
                                             [&](const auto& value) { return value.relay == info.relay_peer; });
   if (current == record.relay_reservations.end()) {
      record.relay_reservations.push_back(std::move(reservation));
   } else {
      *current = std::move(reservation);
   }
   record.capabilities.add(capabilities::relay);
   record.capabilities.add(capabilities::relay_reservation);
   store.upsert(std::move(record));
}

[[nodiscard]] std::optional<node::impl::relay_reservation_state>
node::impl::remember_inbound_relay_reservation(const peer_id& owner, relay::reservation::options request) {
   auto lock = std::scoped_lock{mutex};
   cleanup_expired_relay_reservations_locked();
   if (inbound_relay_reservations.size() >= options.limits.relay.max_reservations &&
       !inbound_relay_reservations.contains(owner)) {
      ++metrics_value.relay_reservation_rejections;
      return std::nullopt;
   }
   if (!inbound_relay_reservations.contains(owner) &&
       !resources.try_acquire_relay_reservation(
           resource_manager::scope{.peer = owner, .protocol = builtins::relay_hop})) {
      ++metrics_value.relay_reservation_rejections;
      return std::nullopt;
   }
   const auto ttl = std::min(request.ttl, options.limits.relay.reservation_ttl);
   auto reservation = relay_reservation_state{
       .owner = owner,
       .relay_peer = local,
       .id = next_reservation_id++,
       .expires_at = std::chrono::steady_clock::now() + ttl,
       .max_streams = std::min(request.max_streams, options.limits.relay.max_streams_per_reservation),
       .max_bytes = std::min(request.max_bytes, options.limits.relay.max_relay_bytes),
       .max_queued_bytes = std::min(request.max_queued_bytes, options.limits.relay.max_queued_bytes),
   };
   inbound_relay_reservations[owner] = reservation;
   metrics_value.active_relay_reservations = inbound_relay_reservations.size();
   ++metrics_value.relay_reservations;
   return reservation;
}

bool node::impl::cancel_inbound_relay_reservation(const peer_id& owner, std::uint64_t reservation_id) {
   auto lock = std::scoped_lock{mutex};
   cleanup_expired_relay_reservations_locked();
   const auto it = inbound_relay_reservations.find(owner);
   if (it == inbound_relay_reservations.end() || (reservation_id != 0 && it->second.id != reservation_id)) {
      return false;
   }
   resources.release_relay_reservation(
       resource_manager::scope{.peer = it->second.owner, .protocol = builtins::relay_hop});
   inbound_relay_reservations.erase(it);
   metrics_value.active_relay_reservations = inbound_relay_reservations.size();
   return true;
}

relay::status node::impl::begin_relay(const peer_id& owner) {
   auto lock = std::scoped_lock{mutex};
   cleanup_expired_relay_reservations_locked();
   if (metrics_value.active_relays >= options.limits.relay.max_active_relays ||
       !resources.try_acquire_relay_stream()) {
      ++metrics_value.relay_rejections;
      return relay::status::resource_limit_exceeded;
   }
   if (options.limits.relay.require_reservation) {
      const auto reservation = inbound_relay_reservations.find(owner);
      if (reservation == inbound_relay_reservations.end()) {
         resources.release_relay_stream();
         ++metrics_value.relay_rejections;
         return relay::status::no_reservation;
      }
      if (reservation->second.active_streams >= reservation->second.max_streams ||
          reservation->second.bytes >= reservation->second.max_bytes) {
         resources.release_relay_stream();
         ++metrics_value.relay_rejections;
         return relay::status::resource_limit_exceeded;
      }
      ++reservation->second.active_streams;
   }
   ++metrics_value.active_relays;
   ++metrics_value.relays_opened;
   return relay::status::ok;
}

[[nodiscard]] std::uint64_t node::impl::relay_byte_limit(const peer_id& owner) {
   auto lock = std::scoped_lock{mutex};
   cleanup_expired_relay_reservations_locked();
   const auto reservation = inbound_relay_reservations.find(owner);
   if (reservation != inbound_relay_reservations.end()) {
      return reservation->second.max_bytes;
   }
   return options.limits.relay.max_relay_bytes;
}

void node::impl::finish_relay(const peer_id& owner) {
   auto lock = std::scoped_lock{mutex};
   auto reservation = inbound_relay_reservations.find(owner);
   if (reservation != inbound_relay_reservations.end() && reservation->second.active_streams > 0) {
      --reservation->second.active_streams;
   }
   if (metrics_value.active_relays > 0) {
      --metrics_value.active_relays;
   }
   resources.release_relay_stream();
}

bool node::impl::add_relay_bytes(const peer_id& owner, std::uint64_t bytes) {
   auto lock = std::scoped_lock{mutex};
   if (!resources.add_relay_bytes(bytes)) {
      ++metrics_value.relay_rejections;
      return false;
   }
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

void node::impl::record_path_open(path::kind kind) {
   auto lock = std::scoped_lock{mutex};
   if (kind == path::kind::direct) {
      ++metrics_value.path_direct_opens;
   } else {
      ++metrics_value.path_relay_opens;
   }
}

void node::impl::record_path_attempt(path::kind kind) {
   auto lock = std::scoped_lock{mutex};
   if (kind == path::kind::direct) {
      ++metrics_value.path_direct_attempts;
   } else {
      ++metrics_value.path_relay_attempts;
   }
}

void node::impl::record_hole_punch_result(hole_punch::status status) {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.hole_punch_attempts;
   if (status == hole_punch::status::succeeded) {
      ++metrics_value.hole_punch_successes;
   } else if (status == hole_punch::status::failed) {
      ++metrics_value.hole_punch_failures;
   }
}

void node::impl::record_direct_failure(const peer_id& peer) {
   store.mark_failure(peer);
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.direct_failures;
}

void node::impl::record_relay_failure() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.relay_failures;
}

void node::impl::increment_dht_query() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.dht_queries;
}

void node::impl::increment_dht_response() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.dht_responses;
}

void node::impl::increment_rendezvous_registration() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.rendezvous_registrations;
}

void node::impl::increment_rendezvous_discover() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.rendezvous_discovers;
}

boost::asio::awaitable<std::shared_ptr<node::impl::session_state>> node::impl::connect_direct(fcl::quic::endpoint endpoint,
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
          .info = node::session_info{.remote_peer = remote,
                                     .capabilities = options.capabilities,
                                     .path = path::kind::direct},
          .connection = std::move(*connection),
          .direct_endpoint = endpoint_copy,
      });
      remember_session(session);
      launch_session_accept_loop(session);
      co_return session;
   } catch (const fcl::exception::base& error) {
      if (deadline && deadline->timed_out()) {
         throw_operation_timeout("P2P connect");
      }
      rethrow_quic_as_p2p(error);
   }
}

boost::asio::awaitable<std::shared_ptr<node::impl::session_state>> node::impl::ensure_direct_session(
    const peer_id& peer, std::chrono::milliseconds timeout, std::size_t max_direct_endpoints,
    std::chrono::milliseconds direct_attempt_timeout) {
   if (auto existing = session_for(peer)) {
      co_return existing;
   }
   const auto record = store.find(peer);
   if (!record || record->endpoints.empty()) {
      exceptions::raise(exceptions::code::peer_not_found, "P2P peer has no known direct endpoint");
   }
   if (max_direct_endpoints == 0) {
      exceptions::raise(exceptions::code::invalid_options, "P2P max direct endpoints must be positive");
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
   auto last_kind = std::optional<exceptions::code>{};
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
      } catch (const fcl::exception::base& error) {
         last_kind = p2p_code(error);
         last_message = error.what();
         store.mark_endpoint_failure(peer, endpoint, path::kind::direct,
                                     std::chrono::system_clock::now() + std::chrono::seconds{5});
         record_direct_failure(peer);
      }
   }
   if (last_kind) {
      exceptions::raise(*last_kind, last_message);
   }
   exceptions::raise(exceptions::code::peer_not_found, "P2P peer has no direct endpoint outside backoff");
}

boost::asio::awaitable<fcl::p2p::stream> node::impl::open_protocol_direct(
    const peer_id& peer, const protocol_id& protocol, std::chrono::milliseconds timeout,
    std::size_t max_direct_endpoints, std::chrono::milliseconds direct_attempt_timeout) {
   const auto started = std::chrono::steady_clock::now();
   auto last_kind = std::optional<exceptions::code>{};
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
         increment_opened_protocol();
         record_path_open(path::kind::direct);
         co_return selected;
      } catch (const fcl::exception::base& error) {
         if (!deadline.finish() || deadline.timed_out()) {
            session->closed = true;
            forget_session(peer);
            if (session->direct_endpoint) {
               store.mark_endpoint_failure(peer, *session->direct_endpoint, path::kind::direct,
                                           std::chrono::system_clock::now() + std::chrono::seconds{5});
            }
            record_direct_failure(peer);
            last_kind = exceptions::code::timeout;
            last_message = "P2P protocol open timed out";
            continue;
         }
         const auto p2p_kind = exceptions::code_of(error);
         if (p2p_kind == exceptions::code::unsupported_protocol || p2p_kind == exceptions::code::protocol_error ||
             p2p_kind == exceptions::code::codec_error) {
            throw;
         }
         session->closed = true;
         forget_session(peer);
         if (session->direct_endpoint) {
            store.mark_endpoint_failure(peer, *session->direct_endpoint, path::kind::direct,
                                        std::chrono::system_clock::now() + std::chrono::seconds{5});
         }
         record_direct_failure(peer);
         last_kind = p2p_kind ? *p2p_kind : map_quic_error(quic_code(error));
         last_message = error.what();
         continue;
      }
   }
   if (last_kind) {
      exceptions::raise(*last_kind, last_message);
   }
   exceptions::raise(exceptions::code::peer_not_found, "P2P direct path attempts were exhausted");
}

boost::asio::awaitable<relay::reservation::info>
node::impl::request_relay_reservation(const peer_id& relay_peer, relay::reservation::options reservation_options,
                          std::chrono::milliseconds timeout) {
   validate_operation_timeout(timeout, "P2P relay reservation timeout");
   if (!options.relay_policy.client_enabled) {
      exceptions::raise(exceptions::code::relay_not_available, "P2P relay client policy is disabled");
   }
   if (reservation_options.ttl.count() <= 0 || reservation_options.max_streams == 0 ||
       reservation_options.max_bytes == 0 || reservation_options.max_queued_bytes == 0) {
      exceptions::raise(exceptions::code::invalid_options, "invalid P2P relay reservation options");
   }
   const auto started = std::chrono::steady_clock::now();
   auto relay_session = co_await ensure_direct_session(relay_peer, timeout);
   auto deadline =
       operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P relay reservation")};
   deadline.arm([relay_session] { relay_session->connection.cancel(); });
   try {
      auto stream = co_await protocol_negotiation::async_select(
          co_await relay_session->connection.async_open_stream(), builtins::relay_hop);
      co_await stream.async_write(
          relay::codec::encode_hop(relay::hop_message{.kind = relay::hop_message::message_kind::reserve}));
      auto relay_buffer = std::vector<std::uint8_t>{};
      auto response = relay::codec::decode_hop(
          co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
      if (!deadline.finish()) {
         throw_operation_timeout("P2P relay reservation");
      }
      if (response.kind != relay::hop_message::message_kind::status || response.status != relay::status::ok ||
          !response.reservation_value) {
         exceptions::raise(response.kind == relay::hop_message::message_kind::status ? exceptions::code::relay_rejected
                                                                                   : exceptions::code::protocol_error,
                         "P2P relay reservation rejected");
      }
      const auto now_seconds =
          std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
      const auto expires_at =
          std::chrono::seconds{static_cast<std::int64_t>(response.reservation_value->expires_at)};
      const auto ttl = expires_at > now_seconds ? expires_at - now_seconds : std::chrono::seconds{1};
      const auto limit = response.limit_value.value_or(relay::limit{
          .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
          .data = options.limits.relay.max_relay_bytes,
      });
      auto info = relay::reservation::info{
          .relay_peer = relay_peer,
          .id = response.reservation_value->expires_at,
          .expires_at = expires_at,
          .ttl = std::chrono::duration_cast<std::chrono::milliseconds>(ttl),
          .max_streams = reservation_options.max_streams,
          .max_bytes = limit.data == 0 ? reservation_options.max_bytes : limit.data,
          .max_queued_bytes = reservation_options.max_queued_bytes,
          .relay_endpoints = response.reservation_value->relay_endpoints,
          .voucher = response.reservation_value->voucher,
      };
      // libp2p Circuit Relay v2 vouchers are signed envelopes. Keep the
      // envelope bytes intact here; validation belongs to the signed-envelope
      // layer, not to the older FCL-local voucher shape.
      remember_outbound_relay_reservation(relay_reservation_state{
          .owner = local,
          .relay_peer = relay_peer,
          .id = info.id,
          .expires_at = std::chrono::steady_clock::now() + info.ttl,
          .max_streams = info.max_streams,
          .max_bytes = info.max_bytes,
          .max_queued_bytes = info.max_queued_bytes,
      });
      remember_relay_reservation_in_store(info);
      co_return info;
   } catch (const fcl::exception::base& error) {
      if (deadline.timed_out()) {
         relay_session->closed = true;
         forget_session(relay_peer);
         throw_operation_timeout("P2P relay reservation");
      }
      rethrow_quic_as_p2p(error);
   }
}

boost::asio::awaitable<void> node::impl::ensure_relay_reservation(const peer_id& relay_peer, std::chrono::milliseconds timeout) {
   if (has_outbound_relay_reservation(relay_peer)) {
      co_return;
   }
   (void)co_await request_relay_reservation(relay_peer,
                                            relay::reservation::options{
                                                .ttl = options.limits.relay.reservation_ttl,
                                                .max_streams = options.limits.relay.max_streams_per_reservation,
                                                .max_bytes = options.limits.relay.max_relay_bytes,
                                                .max_queued_bytes = options.limits.relay.max_queued_bytes,
                                            },
                                            timeout);
}

boost::asio::awaitable<std::shared_ptr<yamux_session>>
node::impl::open_relay_yamux(const peer_id& peer, const peer_id& relay_peer, std::chrono::milliseconds timeout) {
   const auto started = std::chrono::steady_clock::now();
   record_path_attempt(path::kind::relay);
   auto relay_session = co_await ensure_direct_session(relay_peer, timeout);
   auto deadline =
       operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P relay protocol open")};
   deadline.arm([relay_session] { relay_session->connection.cancel(); });
   try {
      auto stream = co_await protocol_negotiation::async_select(
          co_await relay_session->connection.async_open_stream(), builtins::relay_hop);
      co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
          .kind = relay::hop_message::message_kind::connect,
          .target = relay::peer{.id = peer},
      }));
      auto relay_buffer = std::vector<std::uint8_t>{};
      auto response = relay::codec::decode_hop(
          co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
      if (!deadline.finish()) {
         throw_operation_timeout("P2P relay protocol open");
      }
      if (response.kind != relay::hop_message::message_kind::status || response.status != relay::status::ok) {
         exceptions::raise(response.kind == relay::hop_message::message_kind::status ? exceptions::code::relay_rejected
                                                                                   : exceptions::code::protocol_error,
                         response.kind == relay::hop_message::message_kind::status
                             ? "P2P relay open rejected with status " +
                                   std::to_string(static_cast<std::uint16_t>(response.status))
                             : "P2P relay open rejected with unexpected response");
      }
      record_path_open(path::kind::relay);
      stream = detail::stream_access::with_buffer(std::move(stream), std::move(relay_buffer));
      co_return co_await upgrade_relay_outbound_session(std::move(stream), options, peer);
   } catch (const fcl::exception::base& error) {
      record_relay_failure();
      if (deadline.timed_out()) {
         relay_session->closed = true;
         forget_session(relay_peer);
         throw_operation_timeout("P2P relay protocol open");
      }
      rethrow_quic_as_p2p(error);
   }
}

boost::asio::awaitable<fcl::p2p::stream> node::impl::open_protocol_via_relay(const peer_id& peer, const protocol_id& protocol,
                                                                 const peer_id& relay_peer,
                                                                 std::chrono::milliseconds timeout) {
   auto yamux = co_await open_relay_yamux(peer, relay_peer, timeout);
   trace_relay("outbound upgrade: open yamux stream");
   auto substream = co_await yamux->async_open_stream();
   auto selected = co_await protocol_negotiation::async_select(std::move(substream), protocol);
   co_return selected;
}

boost::asio::awaitable<void> node::impl::request_peer_exchange(const peer_id& peer) {
   auto session = co_await ensure_direct_session(peer);
   try {
      auto framed = fcl::quic::framed_stream{
          co_await session->connection.async_open_stream(),
          frame_codec_for(options),
      };
      co_await peer_exchange_codec::async_write(framed,
                                                peer_exchange_message{
                                                    .kind = peer_exchange_message::type::peer_exchange_request,
                                                    .peer = local,
                                                },
                                                codec_for(options));
      auto response = co_await peer_exchange_codec::async_read(framed, codec_for(options));
      if (response.kind != peer_exchange_message::type::peer_exchange_response) {
         exceptions::raise(exceptions::code::protocol_error, "P2P peer exchange expected response");
      }
      learn_from_message(response);
      increment_peer_exchange();
   } catch (const fcl::exception::base& error) {
      rethrow_quic_as_p2p(error);
   }
}

void node::impl::launch_accept_loop() {
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
             } catch (const std::exception&) {
                auto lock = std::scoped_lock{self->mutex};
                if (self->stopped) {
                   co_return;
                }
                ++self->metrics_value.handshakes_failed;
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

boost::asio::awaitable<void> node::impl::handle_inbound_connection(fcl::quic::connection connection) {
   try {
      const auto remote = verified_peer_id(connection, std::nullopt);
      auto session = std::make_shared<session_state>(session_state{
          .info = node::session_info{.remote_peer = remote,
                                     .capabilities = options.capabilities,
                                     .path = path::kind::direct},
          .connection = std::move(connection),
      });
      remember_session(session);
      launch_session_accept_loop(session);
   } catch (const std::exception&) {
      // The listener owns detached accepts; failed handshakes are reflected in metrics.
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.handshakes_failed;
   } catch (...) {
      // The listener owns detached accepts; failed handshakes are reflected in metrics.
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.handshakes_failed;
   }
   co_return;
}

void node::impl::launch_session_accept_loop(std::shared_ptr<node::impl::session_state> session) {
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

boost::asio::awaitable<void> node::impl::handle_incoming_stream(std::shared_ptr<node::impl::session_state> session, fcl::quic::stream raw) {
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
      if (negotiated.protocol == builtins::autonat_v2_dial_request) {
         co_await handle_autonat_v2_dial_request(session, std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::autonat_v2_dial_back) {
         co_await handle_autonat_v2_dial_back(session, std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::autonat_v1) {
         co_await handle_autonat_v1(std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::relay_hop) {
         co_await handle_relay_hop(session, std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::relay_stop) {
         co_await handle_relay_stop(session, std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::dcutr) {
         co_await handle_dcutr(session, std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::kad_dht) {
         co_await handle_dht(session, std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::rendezvous) {
         co_await handle_rendezvous(session, std::move(negotiated.stream));
         co_return;
      }
      auto handler = handler_for(negotiated.protocol);
      if (!handler) {
         increment_protocol_rejected();
         exceptions::raise(exceptions::code::unsupported_protocol, "unsupported negotiated P2P protocol");
      }
      increment_protocol_accepted();
      co_await (*handler)(node::incoming_protocol_stream{
          .session = session->info,
          .protocol = negotiated.protocol,
          .stream = std::move(negotiated.stream),
      });
   } catch (const std::exception&) {
      increment_protocol_rejected();
   } catch (...) {
      increment_protocol_rejected();
   }
}

boost::asio::awaitable<void> node::impl::handle_ping(fcl::p2p::stream stream) {
   if (!begin_ping_stream()) {
      exceptions::raise(exceptions::code::backpressure_rejected, "libp2p ping inbound stream limit reached");
   }
   try {
      while (true) {
         auto payload = co_await stream.async_read();
         if (payload.size() != 32) {
            exceptions::raise(exceptions::code::protocol_error, "libp2p ping payload must be 32 bytes");
         }
         co_await stream.async_write(payload);
      }
   } catch (const fcl::exception::base& error) {
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

boost::asio::awaitable<void> node::impl::handle_identify(fcl::p2p::stream stream) {
   auto encoded = identify::encode(local_identify_document());
   co_await stream.async_write(wrap_length_delimited(encoded));
   co_await stream.async_close();
}

boost::asio::awaitable<void> node::impl::handle_identify_push(std::shared_ptr<node::impl::session_state> session, fcl::p2p::stream stream) {
   auto buffer = std::vector<std::uint8_t>{};
   auto payload = unwrap_length_delimited(
       co_await async_read_length_delimited(stream, buffer, options.limits.max_peer_exchange_message_size),
       options.limits.max_peer_exchange_message_size);
   learn_from_identify(session->info.remote_peer, identify::decode(payload));
   co_await stream.async_close();
}

boost::asio::awaitable<void> node::impl::handle_autonat_v2_dial_back(std::shared_ptr<node::impl::session_state> session,
                                                         fcl::p2p::stream stream) {
   auto buffer = std::vector<std::uint8_t>{};
   auto request = reachability::codec::decode_v2_dial_back(
       co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
   if (request.nonce == 0 || !consume_autonat_v2_nonce(session->info.remote_peer, request.nonce)) {
      exceptions::raise(exceptions::code::protocol_error, "AutoNAT v2 dial-back nonce mismatch");
   }
   co_await stream.async_write(reachability::codec::encode_v2_dial_back_response(
       reachability::v2::dial_back_response{.status = reachability::v2::dial_back_status::ok}));
   co_await stream.async_close();
}

boost::asio::awaitable<void> node::impl::handle_autonat_v2_dial_request(std::shared_ptr<node::impl::session_state> session,
                                                            fcl::p2p::stream stream) {
   auto buffer = std::vector<std::uint8_t>{};
   auto request = reachability::codec::decode_v2(
       co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
   auto response = reachability::v2::dial_response{
       .status = reachability::v2::response_status::request_rejected,
       .index = 0,
       .dial_status = reachability::v2::dial_status::unused,
   };
   if (request.type == reachability::v2::message::kind::dial_request && request.dial_request &&
       !request.dial_request->endpoints.empty() && request.dial_request->nonce != 0) {
      response.status = reachability::v2::response_status::dial_refused;
      response.dial_status = reachability::v2::dial_status::dial_error;
      const auto limit = std::min<std::uint64_t>(4096, reachability::options{}.max_data_response_size);
      for (std::size_t index = 0; index < request.dial_request->endpoints.size(); ++index) {
         const auto& candidate = request.dial_request->endpoints[index];
         co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
             .type = reachability::v2::message::kind::dial_data_request,
             .dial_data_request =
                 reachability::v2::dial_data_request{
                     .index = static_cast<std::uint32_t>(index),
                     .bytes = limit,
                 },
         }));
         const auto data = reachability::codec::decode_v2(
             co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
         if (data.type != reachability::v2::message::kind::dial_data_response || !data.dial_data_response ||
             data.dial_data_response->data.size() < limit) {
            response.status = reachability::v2::response_status::request_rejected;
            response.dial_status = reachability::v2::dial_status::dial_error;
            break;
         }
         response.index = static_cast<std::uint32_t>(index);
         try {
            auto dialed =
                co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                       .expected_peer = session->info.remote_peer,
                                                                       .allow_relay = false,
                                                                       .timeout = std::chrono::milliseconds{1'500},
                                                                   });
            try {
               auto dial_back = co_await protocol_negotiation::async_select(
                   co_await dialed->connection.async_open_stream(), builtins::autonat_v2_dial_back);
               co_await dial_back.async_write(reachability::codec::encode_v2_dial_back(
                   reachability::v2::dial_back{.nonce = request.dial_request->nonce}));
               auto dial_back_buffer = std::vector<std::uint8_t>{};
               const auto dial_back_response =
                   reachability::codec::decode_v2_dial_back_response(co_await async_read_length_delimited(
                       dial_back, dial_back_buffer, reachability::options{}.max_message_size));
               if (dial_back_response.status == reachability::v2::dial_back_status::ok) {
                  response.status = reachability::v2::response_status::ok;
                  response.dial_status = reachability::v2::dial_status::ok;
                  break;
               }
               response.status = reachability::v2::response_status::ok;
               response.dial_status = reachability::v2::dial_status::dial_back_error;
            } catch (...) {
               response.status = reachability::v2::response_status::ok;
               response.dial_status = reachability::v2::dial_status::dial_back_error;
            }
         } catch (const fcl::exception::base& error) {
            response.status = reachability::v2::response_status::ok;
            response.dial_status = p2p_code(error) == exceptions::code::peer_verification_failed
                                       ? reachability::v2::dial_status::dial_back_error
                                       : reachability::v2::dial_status::dial_error;
         } catch (...) {
            response.status = reachability::v2::response_status::ok;
            response.dial_status = reachability::v2::dial_status::dial_error;
         }
      }
   }
   co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
       .type = reachability::v2::message::kind::dial_response,
       .dial_response = std::move(response),
   }));
   co_await stream.async_close();
}

boost::asio::awaitable<void> node::impl::handle_autonat_v1(fcl::p2p::stream stream) {
   auto request = reachability::codec::decode_v1(co_await stream.async_read());
   auto response = reachability::dial_response{
       .status = reachability::dial_status::bad_request,
       .status_text = "expected AutoNAT dial request",
   };
   if (request.kind == reachability::message::message_kind::dial && request.peer &&
       !request.peer->endpoints.empty()) {
      response.status = reachability::dial_status::dial_error;
      response.status_text = "dial failed";
      for (const auto& candidate : request.peer->endpoints) {
         try {
            auto session =
                co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                       .expected_peer = request.peer->peer,
                                                                       .allow_relay = false,
                                                                       .timeout = std::chrono::milliseconds{1'500},
                                                                   });
            session->closed = true;
            forget_session(request.peer->peer);
            try {
               co_await session->connection.async_close();
            } catch (...) {
               session->connection.cancel();
            }
            response.status = reachability::dial_status::ok;
            response.status_text.clear();
            response.endpoint = candidate;
            break;
         } catch (const fcl::exception::base& error) {
            response.status = p2p_code(error) == exceptions::code::peer_verification_failed
                                  ? reachability::dial_status::dial_refused
                                  : reachability::dial_status::dial_error;
         } catch (...) {
            response.status = reachability::dial_status::dial_error;
         }
      }
   }
   co_await stream.async_write(reachability::codec::encode_v1(reachability::message{
       .kind = reachability::message::message_kind::dial_response,
       .response = std::move(response),
   }));
   co_await stream.async_close();
}

boost::asio::awaitable<void> node::impl::handle_relayed_yamux_stream(std::shared_ptr<node::impl::session_state> session,
                                                         fcl::p2p::stream stream) {
   auto negotiated = co_await protocol_negotiation::async_accept(std::move(stream), supported_protocols());
   trace_relay(std::string{"relayed yamux: negotiated "} + negotiated.protocol.value);
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
   if (negotiated.protocol == builtins::dcutr) {
      co_await handle_dcutr(session, std::move(negotiated.stream));
      co_return;
   }
   if (negotiated.protocol == builtins::kad_dht) {
      co_await handle_dht(session, std::move(negotiated.stream));
      co_return;
   }
   if (negotiated.protocol == builtins::rendezvous) {
      co_await handle_rendezvous(session, std::move(negotiated.stream));
      co_return;
   }
   auto handler = handler_for(negotiated.protocol);
   if (!handler) {
      increment_protocol_rejected();
      exceptions::raise(exceptions::code::unsupported_protocol, "unsupported negotiated relayed P2P protocol");
   }
   increment_protocol_accepted();
   co_await (*handler)(node::incoming_protocol_stream{
       .session = session->info,
       .protocol = negotiated.protocol,
       .stream = std::move(negotiated.stream),
   });
}

boost::asio::awaitable<void> node::impl::handle_relay_stop(std::shared_ptr<node::impl::session_state> session, fcl::p2p::stream stream) {
   auto relay_buffer = std::vector<std::uint8_t>{};
   auto request = relay::codec::decode_stop(
       co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
   trace_relay("stop: request decoded");
   if (request.kind != relay::stop_message::message_kind::connect || !request.source) {
      co_await stream.async_write(relay::codec::encode_stop(relay::stop_message{
          .kind = relay::stop_message::message_kind::status,
          .status = relay::status::malformed_message,
      }));
      co_return;
   }
   co_await stream.async_write(relay::codec::encode_stop(relay::stop_message{
       .kind = relay::stop_message::message_kind::status,
       .limit_value = request.limit_value,
       .status = relay::status::ok,
   }));
   trace_relay("stop: ok sent");

   stream = detail::stream_access::with_buffer(std::move(stream), std::move(relay_buffer));
   auto yamux = co_await upgrade_relay_inbound_session(std::move(stream), options, request.source->id);
   auto dcutr_started = false;
   while (true) {
      trace_relay("stop: accepting yamux stream");
      auto relayed_stream = co_await yamux->async_accept_stream();
      auto relayed = session->info;
      relayed.remote_peer = request.source->id;
      relayed.path = path::kind::relay;
      relayed.relay_peer = session->info.remote_peer;
      auto relayed_session = std::make_shared<session_state>();
      relayed_session->info = std::move(relayed);
      co_await handle_relayed_yamux_stream(relayed_session, std::move(relayed_stream));
      if (!dcutr_started && options.capabilities.has(capabilities::hole_punching)) {
         dcutr_started = true;
         const auto dcutr_status =
             co_await run_dcutr_initiator(request.source->id, yamux, std::chrono::milliseconds{10'000});
         trace_relay(std::string{"stop: dcutr initiator status="} + std::to_string(static_cast<int>(dcutr_status)));
      }
   }
}

boost::asio::awaitable<void> node::impl::handle_relay_hop(std::shared_ptr<node::impl::session_state> session, fcl::p2p::stream stream) {
   auto relay_buffer = std::vector<std::uint8_t>{};
   auto request = relay::codec::decode_hop(
       co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
   trace_relay("hop: request decoded");
   if (request.kind == relay::hop_message::message_kind::reserve) {
      if (!options.relay_policy.service_enabled || !options.capabilities.has(capabilities::relay) ||
          !options.capabilities.has(capabilities::relay_reservation)) {
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay::status::permission_denied,
         }));
         co_return;
      }
      if (session->info.path == path::kind::relay) {
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay::status::permission_denied,
         }));
         co_return;
      }
      auto reservation = remember_inbound_relay_reservation(
          session->info.remote_peer, relay::reservation::options{
                                         .ttl = options.limits.relay.reservation_ttl,
                                         .max_streams = options.limits.relay.max_streams_per_reservation,
                                         .max_bytes = options.limits.relay.max_relay_bytes,
                                         .max_queued_bytes = options.limits.relay.max_queued_bytes,
                                     });
      if (!reservation) {
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay::status::reservation_refused,
         }));
         co_return;
      }
      auto endpoints = std::vector<endpoint>{};
      if (auto current = local_endpoint_for_control()) {
         endpoints.push_back(p2p_endpoint_for(*current));
      }
      const auto expires_at = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch() + options.limits.relay.reservation_ttl);
      auto voucher = std::optional<signed_envelope>{};
      if (!options.public_key.empty()) {
         const auto private_key = private_key_from_pem(options.private_key_pem);
         voucher = relay::codec::seal_reservation_voucher(
             relay::voucher{
                 .relay_peer = local,
                 .peer = session->info.remote_peer,
                 .expires_at = static_cast<std::uint64_t>(expires_at.count()),
             },
             decode_public_key(options.public_key), private_key);
      }
      co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
          .kind = relay::hop_message::message_kind::status,
          .reservation_value =
              relay::reservation{
                  .expires_at = static_cast<std::uint64_t>(expires_at.count()),
                  .relay_endpoints = std::move(endpoints),
                  .voucher = std::move(voucher),
              },
          .limit_value =
              relay::limit{
                  .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
                  .data = options.limits.relay.max_relay_bytes,
              },
          .status = relay::status::ok,
      }));
      trace_relay("hop: reserve ok sent");
      co_await stream.async_close();
      co_return;
   }

   if (request.kind != relay::hop_message::message_kind::connect || !request.target) {
      co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
          .kind = relay::hop_message::message_kind::status,
          .status = relay::status::malformed_message,
      }));
      co_return;
   }
   if (!options.relay_policy.service_enabled) {
      co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
          .kind = relay::hop_message::message_kind::status,
          .status = relay::status::permission_denied,
      }));
      co_return;
   }
   const auto relay_owner = request.target->id;
   const auto relay_status = begin_relay(relay_owner);
   trace_relay("hop: connect begin");
   if (relay_status != relay::status::ok) {
      co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
          .kind = relay::hop_message::message_kind::status,
          .status = relay_status,
      }));
      co_return;
   }

   auto target = std::optional<fcl::p2p::stream>{};
   try {
      auto target_session = co_await ensure_direct_session(request.target->id);
      target.emplace(co_await protocol_negotiation::async_select(
          co_await target_session->connection.async_open_stream(), builtins::relay_stop));
      trace_relay("hop: stop selected");
      co_await target->async_write(relay::codec::encode_stop(relay::stop_message{
          .kind = relay::stop_message::message_kind::connect,
          .source = relay::peer{.id = session->info.remote_peer},
          .limit_value =
              relay::limit{
                  .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
                  .data = options.limits.relay.max_relay_bytes,
              },
      }));
      auto stop_buffer = std::vector<std::uint8_t>{};
      const auto stop_status = relay::codec::decode_stop(
          co_await async_read_length_delimited(*target, stop_buffer, reachability::options{}.max_message_size));
      trace_relay("hop: stop status decoded");
      if (stop_status.kind != relay::stop_message::message_kind::status || stop_status.status != relay::status::ok) {
         target.reset();
      }
   } catch (...) {
      target.reset();
   }
   if (!target) {
      finish_relay(relay_owner);
      co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
          .kind = relay::hop_message::message_kind::status,
          .status = relay::status::connection_failed,
      }));
      co_return;
   }

   co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
       .kind = relay::hop_message::message_kind::status,
       .limit_value =
           relay::limit{
               .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
               .data = options.limits.relay.max_relay_bytes,
           },
       .status = relay::status::ok,
   }));
   trace_relay("hop: connect ok sent, starting pumps");
   stream = detail::stream_access::with_buffer(std::move(stream), std::move(relay_buffer));
   launch_relay_pumps(relay_owner, std::move(stream), std::move(*target));
}

boost::asio::awaitable<void> node::impl::handle_dcutr(std::shared_ptr<node::impl::session_state> session, fcl::p2p::stream stream) {
   trace_relay("dcutr: waiting connect");
   auto buffer = std::vector<std::uint8_t>{};
   auto first = co_await async_read_length_delimited(stream, buffer, hole_punch::options{}.max_message_size);
   trace_relay(std::string{"dcutr: connect bytes="} + std::to_string(first.size()));
   auto request = hole_punch::codec::decode(first);
   if (request.kind != hole_punch::message::message_kind::connect) {
      exceptions::raise(exceptions::code::protocol_error, "DCUtR expected CONNECT");
   }
   auto observed = std::vector<endpoint>{};
   if (auto endpoint = local_endpoint_for_control()) {
      observed.push_back(p2p_endpoint_for(*endpoint));
   }
   co_await stream.async_write(hole_punch::codec::encode(hole_punch::message{
       .kind = hole_punch::message::message_kind::connect,
       .observed_endpoints = std::move(observed),
   }));
   trace_relay("dcutr: connect sent, waiting sync");
   auto sync_bytes = co_await async_read_length_delimited(stream, buffer, hole_punch::options{}.max_message_size);
   trace_relay(std::string{"dcutr: sync bytes="} + std::to_string(sync_bytes.size()));
   auto sync = hole_punch::codec::decode(sync_bytes);
   if (sync.kind != hole_punch::message::message_kind::sync) {
      exceptions::raise(exceptions::code::protocol_error, "DCUtR expected SYNC");
   }
   for (const auto& candidate : request.observed_endpoints) {
      trace_relay(std::string{"dcutr inbound: direct candidate "} + candidate.to_string());
      try {
         (void)co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                      .expected_peer = session->info.remote_peer,
                                                                      .allow_relay = false,
                                                                      .timeout = std::chrono::milliseconds{5'000},
                                                                  });
         record_hole_punch_result(hole_punch::status::succeeded);
         co_return;
      } catch (const std::exception& error) {
         trace_relay(std::string{"dcutr inbound: direct failed "} + error.what());
         record_direct_failure(session->info.remote_peer);
      } catch (...) {
         trace_relay("dcutr inbound: direct failed");
         record_direct_failure(session->info.remote_peer);
      }
   }
   if (co_await wait_for_direct_session(session->info.remote_peer, std::chrono::milliseconds{5'000})) {
      record_hole_punch_result(hole_punch::status::succeeded);
      co_return;
   }
   record_hole_punch_result(hole_punch::status::failed);
}

boost::asio::awaitable<void> node::impl::handle_dht(std::shared_ptr<node::impl::session_state> session,
                                                    fcl::p2p::stream stream) {
   if (!options.capabilities.has(capabilities::dht) || options.limits.dht.operating_mode != dht::mode::server) {
      exceptions::raise(exceptions::code::unsupported_protocol, "DHT server mode is disabled");
   }
   auto buffer = std::vector<std::uint8_t>{};
   auto request = dht::codec::decode(co_await async_read_length_delimited(stream, buffer, options.limits.dht.max_message_size),
                                     options.limits.dht);
   increment_dht_query();
   for (const auto& peer : request.closer_peers) {
      store.upsert_routing_peer(peer, discovery::source::dht,
                                std::chrono::system_clock::now() + options.limits.dht.refresh_interval);
   }
   for (const auto& peer : request.provider_peers) {
      store.upsert_routing_peer(peer, discovery::source::dht,
                                std::chrono::system_clock::now() + options.limits.dht.refresh_interval);
   }

   auto response = dht::message{
       .type = request.type,
       .key_value = request.key_value,
   };
   if (request.type == dht::message_type::find_node) {
      response.closer_peers = store.closest_routing_peers(request.key_value, options.limits.dht.replication);
   } else if (request.type == dht::message_type::get_providers) {
      const auto providers = store.find_providers(request.key_value);
      response.provider_peers.reserve(providers.size());
      for (const auto& provider : providers) {
         response.provider_peers.push_back(provider.provider);
      }
      response.closer_peers = store.closest_routing_peers(request.key_value, options.limits.dht.replication);
   } else if (request.type == dht::message_type::add_provider) {
      for (const auto& provider : request.provider_peers) {
         if (provider.id != session->info.remote_peer) {
            continue;
         }
         store.upsert_provider(peer_store::provider_record{
             .key = request.key_value,
             .provider = provider,
             .discovered_by = discovery::source::dht,
             .expires_at = std::chrono::system_clock::now() + options.limits.dht.provider_record_ttl,
         });
      }
      response.provider_peers = request.provider_peers;
   } else if (request.type == dht::message_type::put_value && request.record_value) {
      response.record_value = request.record_value;
   } else if (request.type == dht::message_type::get_value) {
      response.closer_peers = store.closest_routing_peers(request.key_value, options.limits.dht.replication);
   }
   increment_dht_response();
   co_await stream.async_write(dht::codec::encode(response, options.limits.dht));
   co_await stream.async_close();
}

boost::asio::awaitable<void> node::impl::handle_rendezvous(std::shared_ptr<node::impl::session_state> session,
                                                           fcl::p2p::stream stream) {
   if (!options.capabilities.has(capabilities::rendezvous) ||
       (options.limits.rendezvous.operating_role != rendezvous::role::server &&
        options.limits.rendezvous.operating_role != rendezvous::role::client_and_server)) {
      exceptions::raise(exceptions::code::unsupported_protocol, "rendezvous server mode is disabled");
   }
   auto buffer = std::vector<std::uint8_t>{};
   auto request = rendezvous::codec::decode(
       co_await async_read_length_delimited(stream, buffer, options.limits.rendezvous.max_message_size),
       options.limits.rendezvous);

   if (request.type == rendezvous::message_type::register_peer && request.register_value) {
      auto response = rendezvous::register_response{.status_value = rendezvous::status::ok};
      auto ttl = request.register_value->ttl.count() == 0 ? options.limits.rendezvous.default_ttl
                                                          : request.register_value->ttl;
      if (ttl < options.limits.rendezvous.min_ttl || ttl > options.limits.rendezvous.max_ttl) {
         response.status_value = rendezvous::status::invalid_ttl;
         response.status_text = "rendezvous registration TTL outside allowed range";
      } else {
         auto endpoints = std::vector<endpoint>{};
         if (const auto record = store.find(session->info.remote_peer)) {
            for (const auto& endpoint : record->endpoints) {
               endpoints.push_back(fcl::p2p::endpoint{
                   .kind = fcl::p2p::endpoint::address_kind::ip4,
                   .host = endpoint.endpoint.host,
                   .port = endpoint.endpoint.port,
                   .peer = session->info.remote_peer,
               });
            }
         }
         store.upsert_rendezvous(rendezvous::registration{
             .namespace_name = request.register_value->namespace_name,
             .peer = session->info.remote_peer,
             .endpoints = std::move(endpoints),
             .signed_peer_record = request.register_value->signed_peer_record,
             .ttl = ttl,
             .expires_at = std::chrono::system_clock::now() + ttl,
         });
         response.ttl = ttl;
         increment_rendezvous_registration();
      }
      co_await stream.async_write(rendezvous::codec::encode(rendezvous::message{
          .type = rendezvous::message_type::register_response,
          .register_response_value = std::move(response),
      },
                                                            options.limits.rendezvous));
      co_await stream.async_close();
      co_return;
   }

   if (request.type == rendezvous::message_type::unregister_peer && request.unregister_value) {
      store.remove_rendezvous(session->info.remote_peer, request.unregister_value->namespace_name);
      co_await stream.async_close();
      co_return;
   }

   if (request.type == rendezvous::message_type::discover && request.discover_value) {
      const auto after = rendezvous::codec::read_cookie(request.discover_value->cookie);
      const auto limit = request.discover_value->limit == 0
                             ? options.limits.rendezvous.max_discover_limit
                             : std::min(request.discover_value->limit, options.limits.rendezvous.max_discover_limit);
      auto registrations = store.discover_rendezvous(request.discover_value->namespace_name, after, limit);
      auto sequence = after;
      for (const auto& registration : registrations) {
         sequence = std::max(sequence, registration.sequence);
      }
      increment_rendezvous_discover();
      co_await stream.async_write(rendezvous::codec::encode(rendezvous::message{
          .type = rendezvous::message_type::discover_response,
          .discover_response_value =
              rendezvous::discover_response{
                  .registrations = std::move(registrations),
                  .cookie = rendezvous::codec::make_cookie(sequence),
                  .status_value = rendezvous::status::ok,
              },
      },
                                                            options.limits.rendezvous));
      co_await stream.async_close();
      co_return;
   }

   co_await stream.async_write(rendezvous::codec::encode(rendezvous::message{
       .type = rendezvous::message_type::discover_response,
       .discover_response_value =
           rendezvous::discover_response{
               .status_value = rendezvous::status::internal_error,
               .status_text = "unexpected rendezvous message",
           },
   },
                                                         options.limits.rendezvous));
   co_await stream.async_close();
}

boost::asio::awaitable<bool> node::impl::wait_for_direct_session(const peer_id& peer, std::chrono::milliseconds timeout) {
   const auto started = std::chrono::steady_clock::now();
   while (std::chrono::steady_clock::now() - started < timeout) {
      if (auto existing = session_for(peer); existing && existing->info.path == path::kind::direct) {
         co_return true;
      }
      auto remaining = timeout - std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started);
      if (remaining <= std::chrono::milliseconds{0}) {
         break;
      }
      auto timer = asio::steady_timer{runtime.context()};
      timer.expires_after(std::min(remaining, std::chrono::milliseconds{50}));
      co_await timer.async_wait(asio::use_awaitable);
   }
   co_return false;
}

boost::asio::awaitable<hole_punch::status>
node::impl::run_dcutr_initiator(const peer_id& peer, std::shared_ptr<yamux_session> yamux, std::chrono::milliseconds timeout) {
   auto observed = std::vector<endpoint>{};
   if (auto local_endpoint = local_endpoint_for_control()) {
      observed.push_back(p2p_endpoint_for(*local_endpoint));
   }
   if (observed.empty()) {
      record_hole_punch_result(hole_punch::status::failed);
      co_return hole_punch::status::failed;
   }
   try {
      trace_relay("dcutr initiator: open yamux stream");
      auto stream = co_await yamux->async_open_stream();
      stream = co_await protocol_negotiation::async_select(std::move(stream), builtins::dcutr);
      const auto sent = std::chrono::steady_clock::now();
      co_await stream.async_write(hole_punch::codec::encode(hole_punch::message{
          .kind = hole_punch::message::message_kind::connect,
          .observed_endpoints = observed,
      }));
      auto dcutr_buffer = std::vector<std::uint8_t>{};
      auto response = hole_punch::codec::decode(
          co_await async_read_length_delimited(stream, dcutr_buffer, hole_punch::options{}.max_message_size));
      const auto rtt =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - sent);
      trace_relay(std::string{"dcutr initiator: response endpoints="} +
                  std::to_string(response.observed_endpoints.size()));
      if (response.kind != hole_punch::message::message_kind::connect || response.observed_endpoints.empty()) {
         record_hole_punch_result(hole_punch::status::failed);
         co_return hole_punch::status::failed;
      }
      co_await stream.async_write(
          hole_punch::codec::encode(hole_punch::message{.kind = hole_punch::message::message_kind::sync}));
      if (rtt > std::chrono::milliseconds{0}) {
         auto timer = asio::steady_timer{runtime.context()};
         timer.expires_after(rtt / 2);
         co_await timer.async_wait(asio::use_awaitable);
      }
      for (const auto& candidate : response.observed_endpoints) {
         trace_relay(std::string{"dcutr initiator: direct candidate "} + candidate.to_string());
         try {
            (void)co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                         .expected_peer = peer,
                                                                         .allow_relay = false,
                                                                         .timeout = timeout,
                                                                     });
            record_hole_punch_result(hole_punch::status::succeeded);
            co_return hole_punch::status::succeeded;
         } catch (const std::exception& error) {
            trace_relay(std::string{"dcutr initiator: direct failed "} + error.what());
            record_direct_failure(peer);
         } catch (...) {
            trace_relay("dcutr initiator: direct failed");
            record_direct_failure(peer);
         }
      }
      if (co_await wait_for_direct_session(peer, std::min(timeout, std::chrono::milliseconds{5'000}))) {
         record_hole_punch_result(hole_punch::status::succeeded);
         co_return hole_punch::status::succeeded;
      }
   } catch (const std::exception& error) {
      trace_relay(std::string{"dcutr initiator: failed "} + error.what());
   } catch (...) {
      trace_relay("dcutr initiator: failed");
   }
   record_hole_punch_result(hole_punch::status::failed);
   co_return hole_punch::status::failed;
}

boost::asio::awaitable<hole_punch::status>
node::impl::serve_relayed_streams_until_hole_punch(peer_id peer, std::optional<peer_id> relay_peer,
                                       std::shared_ptr<yamux_session> yamux, std::chrono::milliseconds timeout) {
   const auto started = std::chrono::steady_clock::now();
   for (auto handled = 0U; handled != 8U; ++handled) {
      if (auto existing = session_for(peer); existing && existing->info.path == path::kind::direct) {
         record_hole_punch_result(hole_punch::status::succeeded);
         co_return hole_punch::status::succeeded;
      }
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
      if (elapsed >= timeout) {
         break;
      }
      auto before = std::uint64_t{0};
      {
         auto lock = std::scoped_lock{mutex};
         before = metrics_value.hole_punch_successes;
      }
      auto relayed_session = std::make_shared<session_state>();
      relayed_session->info = node::session_info{
          .remote_peer = peer,
          .capabilities = capability_set{.bits = capabilities::hole_punching},
          .path = path::kind::relay,
          .relay_peer = relay_peer,
      };
      auto incoming = co_await yamux->async_accept_stream();
      auto negotiated = co_await protocol_negotiation::async_accept(std::move(incoming), supported_protocols());
      trace_relay(std::string{"relayed yamux wait: negotiated "} + negotiated.protocol.value);
      if (negotiated.protocol == builtins::dcutr) {
         co_await handle_dcutr(relayed_session, std::move(negotiated.stream));
      } else if (negotiated.protocol == builtins::identify) {
         co_await handle_identify(std::move(negotiated.stream));
      } else if (negotiated.protocol == builtins::identify_push) {
         co_await handle_identify_push(relayed_session, std::move(negotiated.stream));
      } else if (negotiated.protocol == builtins::ping) {
         auto self = shared_from_this();
         asio::co_spawn(
             runtime.context(),
             [self, stream = std::move(negotiated.stream)]() mutable -> asio::awaitable<void> {
                try {
                   co_await self->handle_ping(std::move(stream));
                } catch (...) {
                   self->increment_protocol_rejected();
                }
             },
             asio::detached);
      } else {
         auto handler = handler_for(negotiated.protocol);
         if (handler) {
            co_await (*handler)(node::incoming_protocol_stream{
                .session = relayed_session->info,
                .protocol = negotiated.protocol,
                .stream = std::move(negotiated.stream),
            });
         }
      }
      auto after = std::uint64_t{0};
      {
         auto lock = std::scoped_lock{mutex};
         after = metrics_value.hole_punch_successes;
      }
      if (after > before) {
         co_return hole_punch::status::succeeded;
      }
   }
   record_hole_punch_result(hole_punch::status::failed);
   co_return hole_punch::status::failed;
}

boost::asio::awaitable<void> node::impl::handle_peer_exchange(fcl::quic::framed_stream framed, std::uint64_t request_id) {
   auto endpoints = std::vector<peer_exchange_message::endpoint_record>{};
   const auto snapshot = store.snapshot();
   for (const auto& record : snapshot) {
      for (const auto& endpoint : record.endpoints) {
         endpoints.push_back(peer_exchange_message::endpoint_record{
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
   co_await peer_exchange_codec::async_write(framed,
                                             peer_exchange_message{
                                                 .kind = peer_exchange_message::type::peer_exchange_response,
                                                 .request_id = request_id,
                                                 .peer = local,
                                                 .endpoints = std::move(endpoints),
                                             },
                                             codec_for(options));
}

void node::impl::launch_relay_pumps(peer_id owner, fcl::p2p::stream left, fcl::p2p::stream right) {
   auto self = shared_from_this();
   struct relay_pair {
      relay_pair(peer_id owner_value, fcl::p2p::stream left_value, fcl::p2p::stream right_value)
          : owner(std::move(owner_value)), left(std::move(left_value)), right(std::move(right_value)) {}

      peer_id owner;
      fcl::p2p::stream left;
      fcl::p2p::stream right;
      std::mutex mutex;
      std::uint32_t finished = 0;
      std::uint64_t left_to_right_bytes = 0;
      std::uint64_t right_to_left_bytes = 0;
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
                auto chunk = co_await pair->left.async_read();
                if (chunk.empty()) {
                   trace_relay("pump left->right empty read");
                   break;
                }
                trace_relay(std::string{"pump left->right bytes="} + std::to_string(chunk.size()));
                const auto limit = self->relay_byte_limit(pair->owner);
                if (pair->left_to_right_bytes > limit - std::min<std::uint64_t>(limit, chunk.size()) ||
                    pair->left_to_right_bytes + chunk.size() > limit ||
                    !self->add_relay_bytes(pair->owner, chunk.size())) {
                   self->record_relay_failure();
                   break;
                }
                pair->left_to_right_bytes += chunk.size();
                co_await pair->right.async_write(chunk);
             }
          } catch (const fcl::exception::base& error) {
             if (!is_orderly_stream_close(error)) {
                self->record_relay_failure();
             }
          } catch (...) {
             trace_relay("pump left->right failed");
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
                auto chunk = co_await pair->right.async_read();
                if (chunk.empty()) {
                   trace_relay("pump right->left empty read");
                   break;
                }
                trace_relay(std::string{"pump right->left bytes="} + std::to_string(chunk.size()));
                const auto limit = self->relay_byte_limit(pair->owner);
                if (pair->right_to_left_bytes > limit - std::min<std::uint64_t>(limit, chunk.size()) ||
                    pair->right_to_left_bytes + chunk.size() > limit ||
                    !self->add_relay_bytes(pair->owner, chunk.size())) {
                   self->record_relay_failure();
                   break;
                }
                pair->right_to_left_bytes += chunk.size();
                co_await pair->left.async_write(chunk);
             }
          } catch (const fcl::exception::base& error) {
             if (!is_orderly_stream_close(error)) {
                self->record_relay_failure();
             }
          } catch (...) {
             trace_relay("pump right->left failed");
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

boost::asio::awaitable<hole_punch::status> node::impl::attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer,
                                                              std::chrono::milliseconds timeout) {
   validate_operation_timeout(timeout, "P2P hole punch timeout");
   if (session_for(peer)) {
      co_return hole_punch::status::succeeded;
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
      exceptions::raise(exceptions::code::relay_not_available, "P2P hole punching requires a relay peer");
   }
   auto observed = std::vector<endpoint>{};
   if (auto local_endpoint = local_endpoint_for_control()) {
      observed.push_back(p2p_endpoint_for(*local_endpoint));
   }
   if (observed.empty()) {
      record_hole_punch_result(hole_punch::status::failed);
      co_return hole_punch::status::failed;
   }
   try {
      auto yamux = co_await open_relay_yamux(peer, *relay_peer, timeout);
      co_return co_await serve_relayed_streams_until_hole_punch(peer, relay_peer, yamux, timeout);
   } catch (...) {
      // DCUtR failures are expected on many NATs; the caller sees a typed status.
   }
   record_hole_punch_result(hole_punch::status::failed);
   co_return hole_punch::status::failed;
}

} // namespace fcl::p2p
