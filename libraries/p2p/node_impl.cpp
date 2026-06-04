module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
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
import fcl.p2p.pubsub;
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
import fcl.transport.exceptions;
import fcl.transport.session;
import fcl.transport.stream;
import fcl.yamux.exceptions;
import fcl.yamux.session;

#include "node_impl.hpp"
#include "protocol_capabilities.hpp"
#include "relay_accounting.hpp"
#include "session_lifecycle.hpp"

namespace fcl::p2p {

namespace asio = boost::asio;

[[nodiscard]] exceptions::code map_transport_error(fcl::transport::exceptions::code kind) noexcept {
   using transport_kind = fcl::transport::exceptions::code;
   switch (kind) {
   case transport_kind::invalid_endpoint:
      return exceptions::code::invalid_options;
   case transport_kind::closed:
      return exceptions::code::closed;
   case transport_kind::canceled:
      return exceptions::code::canceled;
   case transport_kind::frame_too_large:
   case transport_kind::protocol_error:
   case transport_kind::invalid_buffer:
      return exceptions::code::codec_error;
   case transport_kind::unsupported_protocol:
      return exceptions::code::unsupported_protocol;
   case transport_kind::duplicate_registration:
      return exceptions::code::invalid_options;
   }
   return exceptions::code::internal;
}

[[nodiscard]] exceptions::code p2p_code(const fcl::exceptions::base& error) {
   const auto code = exceptions::code_of(error);
   if (code) {
      return *code;
   }
   const auto transport_code = fcl::transport::exceptions::code_of(error);
   if (transport_code) {
      return map_transport_error(*transport_code);
   }
   return exceptions::code::internal;
}

[[noreturn]] void rethrow_transport_as_p2p(const fcl::exceptions::base& error) {
   FCL_THROW_CODE(p2p_code(error), error.what());
}

[[nodiscard]] bool is_orderly_stream_close(const fcl::exceptions::base& error) noexcept {
   return exceptions::is(error, exceptions::code::closed) ||
          fcl::transport::exceptions::is(error, fcl::transport::exceptions::code::closed) ||
          fcl::transport::exceptions::is(error, fcl::transport::exceptions::code::canceled);
}

[[nodiscard]] std::uint64_t random_nonce() {
   const auto bytes = fcl::crypto::random_bytes(8);
   auto out = std::uint64_t{};
   for (auto byte : bytes) {
      out = (out << 8U) | byte;
   }
   return out == 0 ? 1 : out;
}

[[nodiscard]] std::string bytes_key(std::span<const std::uint8_t> bytes) {
   return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::vector<std::uint8_t> uint64_be(std::uint64_t value) {
   auto out = std::vector<std::uint8_t>(8);
   for (auto i = std::size_t{}; i < out.size(); ++i) {
      out[out.size() - 1 - i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU);
   }
   return out;
}

boost::asio::awaitable<std::vector<std::uint8_t>>
async_read_length_delimited(fcl::p2p::stream& stream, std::vector<std::uint8_t>& buffer, std::size_t max_payload_size) {
   while (true) {
      try {
         const auto decoded = fcl::multiformats::varint_decode(buffer);
         if (decoded.value > max_payload_size) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "libp2p protobuf message exceeds max size");
         }
         const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
         if (buffer.size() >= total) {
            auto out = std::vector<std::uint8_t>{buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total)};
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total));
            co_return out;
         }
      } catch (const fcl::multiformats::exceptions::invalid_format& error) {
         if (std::string_view{error.what()}.find("unterminated") == std::string_view::npos) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, error.what());
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
      FCL_THROW_EXCEPTION(exceptions::codec_error, error.what());
   }
   if (decoded.value > max_payload_size) {
      FCL_THROW_EXCEPTION(exceptions::codec_error, "libp2p protobuf message exceeds max size");
   }
   const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
   if (total != bytes.size()) {
      FCL_THROW_EXCEPTION(exceptions::codec_error, "libp2p protobuf message length mismatch");
   }
   return {bytes.begin() + static_cast<std::ptrdiff_t>(decoded.size), bytes.end()};
}

[[nodiscard]] peer_exchange_codec::options codec_for(const node::options& options) noexcept {
   return peer_exchange_codec::options{
       .max_message_size = static_cast<std::uint32_t>(options.limits.max_peer_exchange_message_size),
       .max_endpoint_records = static_cast<std::uint32_t>(options.limits.max_peer_exchange_records),
   };
}

void validate_operation_timeout(std::chrono::milliseconds timeout, std::string_view name) {
   if (timeout.count() <= 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, std::string{name} + " must be positive");
   }
}

[[nodiscard]] std::chrono::milliseconds remaining_timeout(std::chrono::steady_clock::time_point started,
                                                          std::chrono::milliseconds timeout,
                                                          std::string_view operation) {
   validate_operation_timeout(timeout, operation);
   const auto elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
   if (elapsed >= timeout) {
      FCL_THROW_EXCEPTION(exceptions::timeout, std::string{operation} + " timed out");
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
   FCL_THROW_EXCEPTION(exceptions::timeout, std::string{operation} + " timed out");
}

void validate(const node::options& options) {
   if (!options.allow_insecure_test_mode && (options.certificate_pem.empty() || options.private_key_pem.empty())) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "production P2P node requires mTLS certificate and private key");
   }
   if (options.certificate_pem.empty() != options.private_key_pem.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P certificate and private key must be provided together");
   }
   if (options.explicit_peer_id && !valid_peer_id(*options.explicit_peer_id)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "invalid explicit P2P peer id");
   }
   if (options.allow_insecure_test_mode && options.certificate_pem.empty() && !options.explicit_peer_id) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options,
                          "insecure P2P test node without certificate requires explicit peer id");
   }
   if (options.peer_store_backend && options.peer_store_path) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P peer store backend and path are mutually exclusive");
   }
   if (!options.allow_insecure_test_mode && !options.peer_store_backend && !options.peer_store_path) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "production P2P node requires persistent peer store path");
   }
   if (options.peer_store_path && options.peer_store_path->empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P peer store path must not be empty");
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
       options.limits.dht.provider_record_ttl.count() <= 0 || options.limits.rendezvous.default_ttl.count() <= 0 ||
       options.limits.rendezvous.min_ttl.count() <= 0 || options.limits.rendezvous.max_ttl.count() <= 0 ||
       options.limits.rendezvous.min_ttl > options.limits.rendezvous.max_ttl ||
       options.limits.rendezvous.max_namespace_size == 0 || options.limits.rendezvous.max_registrations_per_peer == 0 ||
       options.limits.rendezvous.max_discover_limit == 0 || options.limits.rendezvous.max_message_size == 0 ||
       options.limits.pubsub.limits.max_rpc_size == 0 || options.limits.pubsub.limits.max_message_size == 0 ||
       options.limits.pubsub.limits.max_data_size == 0 || options.limits.pubsub.limits.max_topic_size == 0 ||
       options.limits.pubsub.limits.max_subscriptions == 0 || options.limits.pubsub.limits.max_messages == 0 ||
       options.limits.pubsub.limits.max_control_entries == 0 || options.limits.pubsub.limits.max_message_ids == 0 ||
       options.limits.pubsub.limits.max_peers_per_topic == 0 || options.limits.pubsub.limits.max_topics == 0 ||
       options.limits.pubsub.limits.max_validation_queue == 0 ||
       options.limits.pubsub.limits.max_outbound_queue_bytes == 0 ||
       options.limits.pubsub.limits.heartbeat_initial_delay.count() <= 0 ||
       options.limits.pubsub.limits.heartbeat_interval.count() <= 0 ||
       options.limits.pubsub.limits.fanout_ttl.count() <= 0 ||
       options.limits.pubsub.limits.prune_backoff.count() <= 0 ||
       options.limits.pubsub.limits.unsubscribe_backoff.count() <= 0 || options.limits.pubsub.limits.mesh_n == 0 ||
       options.limits.pubsub.limits.mesh_n_low == 0 ||
       options.limits.pubsub.limits.mesh_n_high < options.limits.pubsub.limits.mesh_n_low ||
       options.limits.pubsub.limits.history_length == 0 || options.limits.pubsub.limits.history_gossip == 0 ||
       options.limits.pubsub.limits.gossip_lazy == 0 || options.limits.pubsub.limits.gossip_factor <= 0.0 ||
       options.limits.pubsub.limits.gossip_retransmission == 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "invalid P2P node limits");
   }
   if (!options.path_policy.allow_direct && !options.path_policy.allow_hole_punch && !options.path_policy.allow_relay) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P path policy must allow at least one path kind");
   }
   if (options.path_policy.max_direct_endpoints == 0 || options.path_policy.max_relay_candidates == 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P path policy limits must be positive");
   }
   if (options.relay_policy.target_reservations == 0 || options.relay_policy.refresh_margin.count() <= 0 ||
       options.relay_policy.max_candidates_per_refresh == 0 ||
       options.relay_policy.max_parallel_reservations == 0 || options.relay_policy.candidate_backoff.count() <= 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P AutoRelay policy limits must be positive");
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
      direct_registry(runtime_value, options), store(peer_store::options{.backend = make_peer_store_backend(options)}) {
}

std::vector<fcl::p2p::endpoint> node::impl::local_endpoints_for_control() const {
   auto lock = std::scoped_lock{mutex};
   return host_addresses::merge_advertised(options.advertised_endpoints, direct_registry.local_endpoints(), local);
}

void node::impl::learn_from_message(const peer_exchange_message& message,
                                    std::optional<fcl::p2p::endpoint> remote_endpoint) {
   if (valid_peer_id(message.peer)) {
      store.upsert(peer_store::record{
          .peer = message.peer,
          .capabilities = message.capabilities,
      });
   }
   for (const auto& endpoint : message.endpoints) {
      if (valid_peer_id(endpoint.peer)) {
         const auto from_sender = endpoint.peer.to_bytes() == message.peer.to_bytes();
         auto context = host_addresses::learning_context{
             .source =
                 from_sender ? host_addresses::source_kind::authenticated : host_addresses::source_kind::third_party,
         };
         if (from_sender) {
            context.remote_endpoint = remote_endpoint;
         }
         if (auto learned = host_addresses::learned(endpoint.endpoint, endpoint.peer, context)) {
            store.learn_endpoint(endpoint.peer, *learned, endpoint.capabilities);
         }
      }
   }
}

[[nodiscard]] identify::document node::impl::local_identify_document() const {
   return identify::document{
       .protocol_version = options.protocol_version,
       .agent_version = options.agent_version,
       .public_key = options.public_key,
       .listen_endpoints = local_endpoints_for_control(),
       .protocols = supported_protocols(),
   };
}

void node::impl::learn_from_identify(const peer_id& peer, const identify::document& document,
                                     std::optional<fcl::p2p::endpoint> remote_endpoint) {
   auto record = store.find(peer).value_or(peer_store::record{.peer = peer});
   record.protocol_version = document.protocol_version;
   record.agent_version = document.agent_version;
   record.public_key = document.public_key;
   record.protocols = document.protocols;
   record.capabilities.bits |= capabilities_for(document.protocols).bits;
   record.signed_peer_record = document.signed_peer_record;
   record.observed_endpoint = document.observed_endpoint ? document.observed_endpoint : record.observed_endpoint;
   const auto context = host_addresses::learning_context{
       .source = host_addresses::source_kind::authenticated,
       .remote_endpoint = std::move(remote_endpoint),
   };
   for (const auto& endpoint : document.listen_endpoints) {
      auto learned = host_addresses::learned(endpoint, peer, context);
      if (!learned) {
         continue;
      }
      const auto exists = std::ranges::any_of(record.endpoints, [&](const peer_store::endpoint_record& current) {
         return current.endpoint.to_string() == learned->to_string();
      });
      if (!exists) {
         record.endpoints.push_back(peer_store::endpoint_record{
             .endpoint = *learned,
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
      FCL_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P max sessions reached");
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
   pubsub_value.outbound_streams.erase(peer);
}

void node::impl::forget_session(const std::shared_ptr<node::impl::session_state>& session) {
   auto lock = std::scoped_lock{mutex};
   if (!detail::erase_current_session(sessions, session)) {
      return;
   }
   metrics_value.active_sessions = sessions.size();
   ++metrics_value.sessions_closed;
   pubsub_value.outbound_streams.erase(session->info.remote_peer);
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
   if (options.capabilities.has(capabilities::peer_exchange)) {
      out.push_back(builtins::peer_exchange);
   }
   if (options.capabilities.has(capabilities::dht) && options.limits.dht.operating_mode == dht::mode::server) {
      out.push_back(builtins::kad_dht);
   }
   if (options.capabilities.has(capabilities::rendezvous) &&
       (options.limits.rendezvous.operating_role == rendezvous::role::server ||
        options.limits.rendezvous.operating_role == rendezvous::role::client_and_server)) {
      out.push_back(builtins::rendezvous);
   }
   if (options.capabilities.has(capabilities::pubsub)) {
      out.push_back(builtins::meshsub_v11);
      if (options.limits.pubsub.allow_v1_0_fallback) {
         out.push_back(builtins::meshsub_v10);
      }
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

[[nodiscard]] bool node::impl::has_fresh_outbound_relay_reservation(const peer_id& relay_peer,
                                                                    std::chrono::milliseconds refresh_margin) {
   auto lock = std::scoped_lock{mutex};
   cleanup_expired_relay_reservations_locked();
   const auto it = outbound_relay_reservations.find(relay_peer);
   if (it == outbound_relay_reservations.end()) {
      return false;
   }
   return it->second.expires_at > std::chrono::steady_clock::now() + refresh_margin;
}

[[nodiscard]] std::vector<peer_id>
node::impl::fresh_outbound_relay_candidates(std::size_t limit, std::chrono::milliseconds refresh_margin) {
   auto out = std::vector<peer_id>{};
   if (limit == 0) {
      return out;
   }
   auto lock = std::scoped_lock{mutex};
   cleanup_expired_relay_reservations_locked();
   auto scored = std::vector<std::pair<double, peer_id>>{};
   scored.reserve(outbound_relay_reservations.size());
   for (const auto& [relay_peer, reservation] : outbound_relay_reservations) {
      if (reservation.expires_at <= std::chrono::steady_clock::now() + refresh_margin) {
         continue;
      }
      const auto record = store.find(relay_peer);
      scored.push_back({record ? record->score : 0.0, relay_peer});
   }
   std::stable_sort(scored.begin(), scored.end(), [](const auto& left, const auto& right) {
      if (left.first != right.first) {
         return left.first > right.first;
      }
      return left.second.to_string() < right.second.to_string();
   });
   for (const auto& [_, relay_peer] : scored) {
      if (out.size() >= limit) {
         break;
      }
      out.push_back(relay_peer);
   }
   return out;
}

bool node::impl::remember_outbound_relay_reservation(node::impl::relay_reservation_state reservation) {
   auto lock = std::scoped_lock{mutex};
   cleanup_expired_relay_reservations_locked();
   outbound_relay_reservations[reservation.relay_peer] = std::move(reservation);
   return true;
}

void node::impl::remember_relay_reservation_in_store(const relay::reservation::info& info) {
   auto record = store.find(info.relay_peer).value_or(peer_store::record{.peer = info.relay_peer});
   auto relay_endpoints = std::vector<fcl::p2p::endpoint>{};
   relay_endpoints.reserve(info.relay_endpoints.size());
   for (const auto& endpoint : info.relay_endpoints) {
      relay_endpoints.push_back(endpoint);
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
   if (!inbound_relay_reservations.contains(owner) && !resources.try_acquire_relay_reservation(resource_manager::scope{
                                                          .peer = owner, .protocol = builtins::relay_hop})) {
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
   if (metrics_value.active_relays >= options.limits.relay.max_active_relays || !resources.try_acquire_relay_stream()) {
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
   auto reservation = inbound_relay_reservations.find(owner);
   auto* reservation_state = reservation == inbound_relay_reservations.end() ? nullptr : &reservation->second;
   return detail::add_relay_bytes(resources, metrics_value, reservation_state, options.limits.relay.require_reservation,
                                  bytes);
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

void node::impl::increment_pubsub_published() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_messages_published;
}

void node::impl::increment_pubsub_received() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_messages_received;
}

void node::impl::increment_pubsub_delivered() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_messages_delivered;
}

void node::impl::increment_pubsub_duplicate() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_duplicates;
}

void node::impl::increment_pubsub_invalid(const peer_id& peer) {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_invalid_messages;
   pubsub_value.scores[peer].invalid_messages += 1;
   pubsub_value.scores[peer].value -= 1.0;
}

void node::impl::increment_pubsub_control() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.pubsub_control_messages;
}

void node::impl::increment_pubsub_backpressure() {
   auto lock = std::scoped_lock{mutex};
   ++metrics_value.backpressure_rejections;
   ++metrics_value.protocol_rejections;
}

std::vector<std::uint8_t> node::impl::next_pubsub_seqno() {
   auto lock = std::scoped_lock{mutex};
   return uint64_be(pubsub_value.next_seqno++);
}

pubsub::snapshot node::impl::pubsub_snapshot() const {
   auto lock = std::scoped_lock{mutex};
   auto mesh_edges = std::size_t{};
   for (const auto& [_, peers] : pubsub_value.mesh) {
      mesh_edges += peers.size();
   }
   return pubsub::snapshot{
       .topics = pubsub_value.handlers.size(),
       .peers = pubsub_value.peer_topics.size(),
       .mesh_edges = mesh_edges,
       .cached_messages = pubsub_value.cache.size(),
       .messages_published = metrics_value.pubsub_messages_published,
       .messages_received = metrics_value.pubsub_messages_received,
       .messages_delivered = metrics_value.pubsub_messages_delivered,
       .duplicates = metrics_value.pubsub_duplicates,
       .invalid_messages = metrics_value.pubsub_invalid_messages,
       .control_messages = metrics_value.pubsub_control_messages,
   };
}

std::vector<pubsub::subscription> node::impl::local_pubsub_subscriptions() const {
   auto lock = std::scoped_lock{mutex};
   auto out = std::vector<pubsub::subscription>{};
   out.reserve(pubsub_value.handlers.size());
   for (const auto& [topic_value, _] : pubsub_value.handlers) {
      out.push_back(pubsub::subscription{.subscribe = true, .subject = pubsub::topic{.value = topic_value}});
   }
   return out;
}

std::vector<peer_id> node::impl::pubsub_candidate_peers(const std::string& topic_value,
                                                        std::optional<peer_id> except) const {
   auto out = std::vector<peer_id>{};
   {
      auto lock = std::scoped_lock{mutex};
      if (const auto mesh = pubsub_value.mesh.find(topic_value); mesh != pubsub_value.mesh.end()) {
         for (const auto& peer : mesh->second) {
            if (!except || peer != *except) {
               out.push_back(peer);
            }
         }
      }
      for (const auto& [peer, topics] : pubsub_value.peer_topics) {
         if (topics.contains(topic_value) && (!except || peer != *except) &&
             std::ranges::find(out, peer) == out.end()) {
            out.push_back(peer);
         }
      }
      for (const auto& [peer, session] : sessions) {
         (void)session;
         if ((!except || peer != *except) && std::ranges::find(out, peer) == out.end()) {
            out.push_back(peer);
         }
      }
   }
   for (const auto& record : store.snapshot()) {
      const auto supports_pubsub = record.capabilities.has(capabilities::pubsub) ||
                                   std::ranges::any_of(record.protocols, [](const protocol_id& protocol) {
                                      return protocol == builtins::meshsub_v11 || protocol == builtins::meshsub_v10;
                                   });
      if (supports_pubsub && (!except || record.peer != *except) && std::ranges::find(out, record.peer) == out.end()) {
         out.push_back(record.peer);
      }
   }
   return out;
}

boost::asio::awaitable<void> node::impl::send_pubsub_rpc(const peer_id& peer, const pubsub::rpc& value) {
   auto protocol = builtins::meshsub_v11;
   if (options.limits.pubsub.allow_v1_0_fallback) {
      const auto record = store.find(peer);
      const auto supports_v11 = record && std::ranges::any_of(record->protocols, [](const protocol_id& value) {
                                   return value == builtins::meshsub_v11;
                                });
      const auto supports_v10 = record && std::ranges::any_of(record->protocols, [](const protocol_id& value) {
                                   return value == builtins::meshsub_v10;
                                });
      if (supports_v10 && !supports_v11) {
         protocol = builtins::meshsub_v10;
      }
   }
   const auto encoded = pubsub::codec::encode(value, options.limits.pubsub);
   if (encoded.size() > options.limits.pubsub.limits.max_outbound_queue_bytes) {
      increment_pubsub_backpressure();
      FCL_THROW_EXCEPTION(exceptions::backpressure_rejected, "GossipSub outbound queue byte limit reached");
   }
   auto outbound = std::shared_ptr<fcl::p2p::stream>{};
   {
      auto lock = std::scoped_lock{mutex};
      if (const auto existing = pubsub_value.outbound_streams.find(peer);
          existing != pubsub_value.outbound_streams.end() && existing->second && existing->second->valid()) {
         outbound = existing->second;
      }
   }
   if (!outbound) {
      auto stream = co_await open_protocol_direct(peer, protocol, options.limits.discovery.query_timeout);
      outbound = std::make_shared<fcl::p2p::stream>(std::move(stream));
      auto lock = std::scoped_lock{mutex};
      pubsub_value.outbound_streams[peer] = outbound;
   }
   try {
      co_await outbound->async_write(encoded);
   } catch (const fcl::exceptions::base&) {
      auto lock = std::scoped_lock{mutex};
      pubsub_value.outbound_streams.erase(peer);
      throw;
   }
}

boost::asio::awaitable<void> node::impl::announce_pubsub_subscriptions(const peer_id& peer) {
   if (!options.capabilities.has(capabilities::pubsub)) {
      co_return;
   }
   auto subscriptions = local_pubsub_subscriptions();
   if (subscriptions.empty()) {
      co_return;
   }
   try {
      co_await send_pubsub_rpc(peer, pubsub::rpc{.subscriptions = std::move(subscriptions)});
   } catch (const fcl::exceptions::base&) {
      store.mark_failure(peer);
   }
}

bool node::impl::try_begin_pubsub_validation(const peer_id& peer) {
   auto lock = std::scoped_lock{mutex};
   if (pubsub_value.active_validations >= options.limits.pubsub.limits.max_validation_queue) {
      ++metrics_value.backpressure_rejections;
      ++metrics_value.protocol_rejections;
      ++metrics_value.pubsub_invalid_messages;
      pubsub_value.scores[peer].invalid_messages += 1;
      pubsub_value.scores[peer].value -= 1.0;
      return false;
   }
   ++pubsub_value.active_validations;
   ++pubsub_value.active_validations_by_peer[peer];
   return true;
}

void node::impl::finish_pubsub_validation(const peer_id& peer) {
   auto lock = std::scoped_lock{mutex};
   if (pubsub_value.active_validations > 0) {
      --pubsub_value.active_validations;
   }
   if (auto it = pubsub_value.active_validations_by_peer.find(peer);
       it != pubsub_value.active_validations_by_peer.end()) {
      if (it->second > 1) {
         --it->second;
      } else {
         pubsub_value.active_validations_by_peer.erase(it);
      }
   }
}

bool node::impl::pubsub_control_over_limit(const pubsub::control& value) const noexcept {
   return value.have.size() > options.limits.pubsub.limits.max_ihave_per_peer ||
          value.want.size() > options.limits.pubsub.limits.max_iwant_per_peer ||
          value.grafts.size() > options.limits.pubsub.limits.max_graft_per_peer;
}

void node::impl::launch_pubsub_heartbeat() {
   if (!options.capabilities.has(capabilities::pubsub)) {
      return;
   }
   {
      auto lock = std::scoped_lock{mutex};
      if (pubsub_value.heartbeat_started) {
         return;
      }
      pubsub_value.heartbeat_started = true;
   }
   auto self = shared_from_this();
   asio::co_spawn(
       runtime.context(),
       [self]() -> asio::awaitable<void> {
          auto timer = asio::steady_timer{co_await asio::this_coro::executor};
          timer.expires_after(self->options.limits.pubsub.limits.heartbeat_initial_delay);
          boost::system::error_code ec;
          co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
          while (true) {
             {
                auto lock = std::scoped_lock{self->mutex};
                if (self->stopped) {
                   co_return;
                }
             }
             co_await self->pubsub_heartbeat_once();
             timer.expires_after(self->options.limits.pubsub.limits.heartbeat_interval);
             ec = {};
             co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
          }
       },
       asio::detached);
}

boost::asio::awaitable<void> node::impl::pubsub_heartbeat_once() {
   auto grafts = std::map<peer_id, std::vector<pubsub::control::graft>>{};
   auto prunes = std::map<peer_id, std::vector<pubsub::control::prune>>{};
   auto gossip = std::map<peer_id, std::vector<pubsub::control::ihave>>{};
   {
      auto lock = std::scoped_lock{mutex};
      if (stopped) {
         co_return;
      }
      const auto mesh_high =
          std::min(options.limits.pubsub.limits.mesh_n_high, options.limits.pubsub.limits.max_peers_per_topic);
      const auto mesh_target = std::min(options.limits.pubsub.limits.mesh_n, mesh_high);
      for (const auto& [topic_value, _] : pubsub_value.handlers) {
         auto& mesh = pubsub_value.mesh[topic_value];
         for (auto it = mesh.begin(); it != mesh.end();) {
            const auto has_session = sessions.contains(*it);
            const auto topics = pubsub_value.peer_topics.find(*it);
            const auto subscribed = topics != pubsub_value.peer_topics.end() && topics->second.contains(topic_value);
            if (!has_session && !subscribed) {
               it = mesh.erase(it);
            } else {
               ++it;
            }
         }
         for (const auto& [peer, topics] : pubsub_value.peer_topics) {
            if (mesh.size() >= mesh_target) {
               break;
            }
            if (topics.contains(topic_value) && !mesh.contains(peer)) {
               mesh.insert(peer);
               grafts[peer].push_back(pubsub::control::graft{.subject = pubsub::topic{.value = topic_value}});
            }
         }
         for (const auto& [peer, _session] : sessions) {
            if (mesh.size() >= mesh_target) {
               break;
            }
            if (!mesh.contains(peer)) {
               mesh.insert(peer);
               grafts[peer].push_back(pubsub::control::graft{.subject = pubsub::topic{.value = topic_value}});
            }
         }
         while (mesh.size() > mesh_high) {
            auto it = std::prev(mesh.end());
            const auto peer = *it;
            mesh.erase(it);
            prunes[peer].push_back(pubsub::control::prune{
                .subject = pubsub::topic{.value = topic_value},
                .backoff = options.limits.pubsub.limits.prune_backoff,
            });
         }
         auto ids = std::vector<std::vector<std::uint8_t>>{};
         auto seen = std::size_t{};
         for (auto it = pubsub_value.history.rbegin();
              it != pubsub_value.history.rend() && seen < options.limits.pubsub.limits.history_gossip; ++it, ++seen) {
            if (const auto found = pubsub_value.cache.find(*it);
                found != pubsub_value.cache.end() && found->second.subject.value == topic_value) {
               ids.push_back(pubsub::codec::message_id(found->second));
            }
         }
         if (!ids.empty()) {
            if (ids.size() > options.limits.pubsub.limits.gossip_lazy) {
               ids.resize(options.limits.pubsub.limits.gossip_lazy);
            }
            for (const auto& peer : mesh) {
               gossip[peer].push_back(pubsub::control::ihave{
                   .subject = pubsub::topic{.value = topic_value},
                   .message_ids = ids,
               });
            }
         }
      }
   }
   for (const auto& [peer, items] : grafts) {
      try {
         co_await send_pubsub_rpc(peer, pubsub::rpc{.control_value = pubsub::control{.grafts = items}});
      } catch (const fcl::exceptions::base&) {
         store.mark_failure(peer);
      }
   }
   for (const auto& [peer, items] : prunes) {
      try {
         co_await send_pubsub_rpc(peer, pubsub::rpc{.control_value = pubsub::control{.prunes = items}});
      } catch (const fcl::exceptions::base&) {
         store.mark_failure(peer);
      }
   }
   for (const auto& [peer, items] : gossip) {
      try {
         co_await send_pubsub_rpc(peer, pubsub::rpc{.control_value = pubsub::control{.have = items}});
      } catch (const fcl::exceptions::base&) {
         store.mark_failure(peer);
      }
   }
}

boost::asio::awaitable<std::shared_ptr<node::impl::session_state>>
node::impl::connect_direct(fcl::p2p::endpoint endpoint, node::connect_options connect_options_value) {
   validate_operation_timeout(connect_options_value.timeout, "P2P connect timeout");
   auto endpoint_copy = endpoint;
   try {
      auto started = std::chrono::steady_clock::now();
      auto result = co_await direct_registry.async_connect(std::move(endpoint), connect_options_value);
      store.mark_endpoint_success(
          result.peer, endpoint_copy, path::kind::direct,
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started));
      auto session = std::make_shared<session_state>(session_state{
          .info = node::session_info{.remote_peer = result.peer,
                                     .capabilities = options.capabilities,
                                     .path = path::kind::direct},
          .connection = std::move(result.session),
          .direct_endpoint = endpoint_copy,
          .remote_endpoint = result.remote_endpoint,
      });
      remember_session(session);
      launch_session_accept_loop(session);
      co_await announce_pubsub_subscriptions(result.peer);
      co_return session;
   } catch (const fcl::exceptions::base& error) {
      FCL_THROW_CODE(p2p_code(error), error.what());
   }
}

boost::asio::awaitable<std::shared_ptr<node::impl::session_state>>
node::impl::ensure_direct_session(const peer_id& peer, std::chrono::milliseconds timeout,
                                  std::size_t max_direct_endpoints, std::chrono::milliseconds direct_attempt_timeout) {
   if (auto existing = session_for(peer)) {
      co_return existing;
   }
   const auto record = store.find(peer);
   if (!record || record->endpoints.empty()) {
      FCL_THROW_EXCEPTION(exceptions::peer_not_found, "P2P peer has no known direct endpoint");
   }
   if (max_direct_endpoints == 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P max direct endpoints must be positive");
   }
   const auto now = std::chrono::system_clock::now();
   auto preferred = path_selector::rank_direct(*record, now);

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
      } catch (const fcl::exceptions::base& error) {
         last_kind = p2p_code(error);
         last_message = error.what();
         store.mark_endpoint_failure(peer, endpoint, path::kind::direct,
                                     std::chrono::system_clock::now() + std::chrono::seconds{5});
         record_direct_failure(peer);
      }
   }
   if (last_kind) {
      FCL_THROW_CODE(*last_kind, last_message);
   }
   FCL_THROW_EXCEPTION(exceptions::peer_not_found, "P2P peer has no direct endpoint outside backoff");
}

boost::asio::awaitable<fcl::p2p::stream>
node::impl::open_protocol_direct(const peer_id& peer, const protocol_id& protocol, std::chrono::milliseconds timeout,
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
      } catch (const fcl::exceptions::base& error) {
         if (!deadline.finish() || deadline.timed_out()) {
            session->closed = true;
            forget_session(session);
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
         forget_session(session);
         if (session->direct_endpoint) {
            store.mark_endpoint_failure(peer, *session->direct_endpoint, path::kind::direct,
                                        std::chrono::system_clock::now() + std::chrono::seconds{5});
         }
         record_direct_failure(peer);
         last_kind = p2p_code(error);
         last_message = error.what();
         continue;
      }
   }
   if (last_kind) {
      FCL_THROW_CODE(*last_kind, last_message);
   }
   FCL_THROW_EXCEPTION(exceptions::peer_not_found, "P2P direct path attempts were exhausted");
}

boost::asio::awaitable<relay::reservation::info>
node::impl::request_relay_reservation(const peer_id& relay_peer, relay::reservation::options reservation_options,
                                      std::chrono::milliseconds timeout) {
   validate_operation_timeout(timeout, "P2P relay reservation timeout");
   if (!options.relay_policy.client_enabled) {
      FCL_THROW_EXCEPTION(exceptions::relay_not_available, "P2P relay client policy is disabled");
   }
   if (reservation_options.ttl.count() <= 0 || reservation_options.max_streams == 0 ||
       reservation_options.max_bytes == 0 || reservation_options.max_queued_bytes == 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "invalid P2P relay reservation options");
   }
   const auto started = std::chrono::steady_clock::now();
   auto relay_session = co_await ensure_direct_session(relay_peer, timeout);
   auto deadline = operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P relay reservation")};
   deadline.arm([relay_session] { relay_session->connection.cancel(); });
   try {
      auto stream = co_await protocol_negotiation::async_select(co_await relay_session->connection.async_open_stream(),
                                                                builtins::relay_hop);
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
         FCL_THROW_CODE(response.kind == relay::hop_message::message_kind::status ? exceptions::code::relay_rejected
                                                                                  : exceptions::code::protocol_error,
                        "P2P relay reservation rejected");
      }
      const auto now_seconds =
          std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
      const auto expires_at = std::chrono::seconds{static_cast<std::int64_t>(response.reservation_value->expires_at)};
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
   } catch (const fcl::exceptions::base& error) {
      if (deadline.timed_out()) {
         relay_session->closed = true;
         forget_session(relay_session);
         throw_operation_timeout("P2P relay reservation");
      }
      rethrow_transport_as_p2p(error);
   }
}

boost::asio::awaitable<void> node::impl::ensure_relay_reservation(const peer_id& relay_peer,
                                                                  std::chrono::milliseconds timeout) {
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

boost::asio::awaitable<std::vector<relay::reservation::info>>
node::impl::refresh_relay_candidates(std::optional<peer_id> target, std::chrono::milliseconds timeout) {
   validate_operation_timeout(timeout, "P2P AutoRelay refresh timeout");
   if (!options.relay_policy.client_enabled) {
      FCL_THROW_EXCEPTION(exceptions::relay_not_available, "P2P relay client policy is disabled");
   }
   if (!options.relay_policy.auto_discovery_enabled) {
      co_return std::vector<relay::reservation::info>{};
   }

   {
      auto lock = std::scoped_lock{mutex};
      if (stopped) {
         FCL_THROW_EXCEPTION(exceptions::closed, "P2P node is stopped");
      }
      ++metrics_value.relay_discovery_refreshes;
   }

   const auto system_now = std::chrono::system_clock::now();
   relay_discovery::prune_expired_reservations(store, system_now);

   const auto target_reservations = options.relay_policy.target_reservations;
   auto fresh_count = fresh_outbound_relay_candidates(target_reservations, options.relay_policy.refresh_margin).size();
   if (fresh_count >= target_reservations) {
      co_return std::vector<relay::reservation::info>{};
   }

   const auto snapshot = store.snapshot();
   auto candidates = relay_discovery::select_candidates(
       snapshot,
       relay_discovery::request{
           .local = local,
           .target = target.value_or(peer_id{}),
           .now = system_now,
           .limit = options.relay_policy.max_candidates_per_refresh,
       });
   auto out = std::vector<relay::reservation::info>{};
   out.reserve(std::min(candidates.size(), target_reservations - fresh_count));

   const auto started = std::chrono::steady_clock::now();
   auto attempts_in_batch = std::size_t{0};
   for (const auto& candidate : candidates) {
      if (fresh_count + out.size() >= target_reservations) {
         break;
      }
      if (attempts_in_batch >= options.relay_policy.max_parallel_reservations) {
         co_await asio::post(runtime.context(), asio::use_awaitable);
         attempts_in_batch = 0;
      }
      if (has_fresh_outbound_relay_reservation(candidate.peer, options.relay_policy.refresh_margin)) {
         ++fresh_count;
         continue;
      }
      ++attempts_in_batch;
      {
         auto lock = std::scoped_lock{mutex};
         ++metrics_value.relay_discovery_attempts;
      }
      try {
         const auto remaining = remaining_timeout(started, timeout, "P2P AutoRelay refresh");
         auto info = co_await request_relay_reservation(
             candidate.peer,
             relay::reservation::options{
                 .ttl = options.limits.relay.reservation_ttl,
                 .max_streams = options.limits.relay.max_streams_per_reservation,
                 .max_bytes = options.limits.relay.max_relay_bytes,
                 .max_queued_bytes = options.limits.relay.max_queued_bytes,
             },
             remaining);
         store.mark_success(candidate.peer, path::kind::relay, std::chrono::milliseconds{0});
         {
            auto lock = std::scoped_lock{mutex};
            ++metrics_value.relay_discovery_successes;
         }
         out.push_back(std::move(info));
      } catch (const fcl::exceptions::base&) {
         relay_discovery::backoff_candidate(store, candidate.peer,
                                            std::chrono::system_clock::now() + options.relay_policy.candidate_backoff);
         {
            auto lock = std::scoped_lock{mutex};
            ++metrics_value.relay_discovery_failures;
         }
      }
   }
   co_return out;
}

void node::impl::launch_relay_discovery_maintenance() {
   if (!options.relay_policy.client_enabled || !options.relay_policy.auto_discovery_enabled) {
      return;
   }
   {
      auto lock = std::scoped_lock{mutex};
      if (relay_discovery_value.maintenance_started) {
         return;
      }
      relay_discovery_value.maintenance_started = true;
   }
   auto self = shared_from_this();
   asio::co_spawn(
       runtime.context(),
       [self]() -> asio::awaitable<void> {
          auto timer = asio::steady_timer{co_await asio::this_coro::executor};
          while (true) {
             timer.expires_after(self->options.limits.discovery.refresh_interval);
             auto ec = boost::system::error_code{};
             co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
             {
                auto lock = std::scoped_lock{self->mutex};
                if (self->stopped) {
                   co_return;
                }
             }
             try {
                (void)co_await self->refresh_relay_candidates(std::nullopt, self->options.limits.discovery.query_timeout);
             } catch (const fcl::exceptions::base&) {
                auto lock = std::scoped_lock{self->mutex};
                ++self->metrics_value.relay_discovery_failures;
             }
          }
       },
       asio::detached);
}

boost::asio::awaitable<std::shared_ptr<fcl::yamux::session>>
node::impl::open_relay_yamux(const peer_id& peer, const peer_id& relay_peer, std::chrono::milliseconds timeout) {
   const auto started = std::chrono::steady_clock::now();
   record_path_attempt(path::kind::relay);
   auto relay_session = co_await ensure_direct_session(relay_peer, timeout);
   auto deadline =
       operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P relay protocol open")};
   deadline.arm([relay_session] { relay_session->connection.cancel(); });
   try {
      auto stream = co_await protocol_negotiation::async_select(co_await relay_session->connection.async_open_stream(),
                                                                builtins::relay_hop);
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
         FCL_THROW_CODE(response.kind == relay::hop_message::message_kind::status ? exceptions::code::relay_rejected
                                                                                  : exceptions::code::protocol_error,
                        response.kind == relay::hop_message::message_kind::status
                            ? "P2P relay open rejected with status " +
                                  std::to_string(static_cast<std::uint16_t>(response.status))
                            : "P2P relay open rejected with unexpected response");
      }
      record_path_open(path::kind::relay);
      stream = detail::stream_access::with_buffer(std::move(stream), std::move(relay_buffer));
      co_return co_await upgrade_relay_outbound_session(std::move(stream), options, peer);
   } catch (const fcl::exceptions::base& error) {
      record_relay_failure();
      if (deadline.timed_out()) {
         relay_session->closed = true;
         forget_session(relay_session);
         throw_operation_timeout("P2P relay protocol open");
      }
      rethrow_transport_as_p2p(error);
   }
}

boost::asio::awaitable<fcl::p2p::stream> node::impl::open_protocol_via_relay(const peer_id& peer,
                                                                             const protocol_id& protocol,
                                                                             const peer_id& relay_peer,
                                                                             std::chrono::milliseconds timeout) {
   auto yamux = co_await open_relay_yamux(peer, relay_peer, timeout);
   trace_relay("outbound upgrade: open yamux stream");
   auto substream = fcl::p2p::stream{co_await yamux->async_open_stream()};
   auto selected = co_await protocol_negotiation::async_select(std::move(substream), protocol);
   co_return selected;
}

boost::asio::awaitable<void> node::impl::request_peer_exchange(const peer_id& peer) {
   auto session = co_await ensure_direct_session(peer);
   try {
      auto stream = co_await protocol_negotiation::async_select(
          fcl::p2p::stream{co_await session->connection.async_open_stream()}, builtins::peer_exchange);
      co_await peer_exchange_codec::async_write(stream,
                                                peer_exchange_message{
                                                    .kind = peer_exchange_message::type::peer_exchange_request,
                                                    .peer = local,
                                                },
                                                codec_for(options));
      auto response = co_await peer_exchange_codec::async_read(stream, codec_for(options));
      if (response.kind != peer_exchange_message::type::peer_exchange_response) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "P2P peer exchange expected response");
      }
      learn_from_message(response, session->remote_endpoint);
      increment_peer_exchange();
      co_await stream.async_close();
   } catch (const fcl::exceptions::base& error) {
      rethrow_transport_as_p2p(error);
   }
}

void node::impl::launch_accept_loop(fcl::p2p::endpoint local_endpoint) {
   auto self = shared_from_this();
   asio::co_spawn(
       runtime.context(),
       [self, local_endpoint = std::move(local_endpoint)]() -> asio::awaitable<void> {
          while (true) {
             {
                auto lock = std::scoped_lock{self->mutex};
                if (self->stopped || !self->direct_registry.listening()) {
                   co_return;
                }
             }
             try {
                auto connection = co_await self->direct_registry.async_accept(local_endpoint);
                asio::co_spawn(
                    self->runtime.context(),
                    [self, connection = std::move(connection)]() mutable -> asio::awaitable<void> {
                       co_await self->handle_inbound_connection(std::move(connection));
                    },
                    asio::detached);
             } catch (const fcl::exceptions::base& error) {
                auto lock = std::scoped_lock{self->mutex};
                if (self->stopped || exceptions::is(error, exceptions::code::closed) ||
                    exceptions::is(error, exceptions::code::canceled)) {
                   co_return;
                }
                ++self->metrics_value.handshakes_failed;
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

boost::asio::awaitable<void> node::impl::handle_inbound_connection(direct::connection connection) {
   try {
      auto remote = connection.peer;
      auto session = std::make_shared<session_state>(session_state{
          .info = node::session_info{.remote_peer = remote,
                                     .capabilities = options.capabilities,
                                     .path = path::kind::direct},
          .connection = std::move(connection.session),
          .direct_endpoint = connection.local_endpoint,
          .remote_endpoint = connection.remote_endpoint,
      });
      remember_session(session);
      launch_session_accept_loop(session);
      co_await announce_pubsub_subscriptions(remote);
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
                self->forget_session(session);
                co_return;
             }
          }
       },
       asio::detached);
}

boost::asio::awaitable<void> node::impl::handle_incoming_stream(std::shared_ptr<node::impl::session_state> session,
                                                                fcl::transport::stream raw) {
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
      if (negotiated.protocol == builtins::peer_exchange) {
         auto request = co_await peer_exchange_codec::async_read(negotiated.stream, codec_for(options));
         if (request.kind != peer_exchange_message::type::peer_exchange_request) {
            FCL_THROW_EXCEPTION(exceptions::protocol_error, "P2P peer exchange expected request");
         }
         co_await handle_peer_exchange(std::move(negotiated.stream), request.request_id);
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
      if (negotiated.protocol == builtins::meshsub_v11 || negotiated.protocol == builtins::meshsub_v10) {
         co_await handle_pubsub(session, std::move(negotiated.stream));
         co_return;
      }
      auto handler = handler_for(negotiated.protocol);
      if (!handler) {
         increment_protocol_rejected();
         FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "unsupported negotiated P2P protocol");
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
      FCL_THROW_EXCEPTION(exceptions::backpressure_rejected, "libp2p ping inbound stream limit reached");
   }
   try {
      while (true) {
         auto payload = co_await stream.async_read();
         if (payload.size() != 32) {
            FCL_THROW_EXCEPTION(exceptions::protocol_error, "libp2p ping payload must be 32 bytes");
         }
         co_await stream.async_write(payload);
      }
   } catch (const fcl::exceptions::base& error) {
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

boost::asio::awaitable<void> node::impl::handle_identify_push(std::shared_ptr<node::impl::session_state> session,
                                                              fcl::p2p::stream stream) {
   auto buffer = std::vector<std::uint8_t>{};
   auto payload = unwrap_length_delimited(
       co_await async_read_length_delimited(stream, buffer, options.limits.max_peer_exchange_message_size),
       options.limits.max_peer_exchange_message_size);
   learn_from_identify(session->info.remote_peer, identify::decode(payload), session->remote_endpoint);
   co_await stream.async_close();
}

boost::asio::awaitable<void> node::impl::handle_autonat_v2_dial_back(std::shared_ptr<node::impl::session_state> session,
                                                                     fcl::p2p::stream stream) {
   auto buffer = std::vector<std::uint8_t>{};
   auto request = reachability::codec::decode_v2_dial_back(
       co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
   if (request.nonce == 0 || !consume_autonat_v2_nonce(session->info.remote_peer, request.nonce)) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "AutoNAT v2 dial-back nonce mismatch");
   }
   co_await stream.async_write(reachability::codec::encode_v2_dial_back_response(
       reachability::v2::dial_back_response{.status = reachability::v2::dial_back_status::ok}));
   co_await stream.async_close();
}

boost::asio::awaitable<void>
node::impl::handle_autonat_v2_dial_request(std::shared_ptr<node::impl::session_state> session,
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
            auto dialed = co_await connect_direct(candidate, node::connect_options{
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
         } catch (const fcl::exceptions::base& error) {
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
   if (request.kind == reachability::message::message_kind::dial && request.peer && !request.peer->endpoints.empty()) {
      response.status = reachability::dial_status::dial_error;
      response.status_text = "dial failed";
      for (const auto& candidate : request.peer->endpoints) {
         try {
            auto session = co_await connect_direct(candidate, node::connect_options{
                                                                  .expected_peer = request.peer->peer,
                                                                  .allow_relay = false,
                                                                  .timeout = std::chrono::milliseconds{1'500},
                                                              });
            session->closed = true;
            forget_session(session);
            try {
               co_await session->connection.async_close();
            } catch (...) {
               session->connection.cancel();
            }
            response.status = reachability::dial_status::ok;
            response.status_text.clear();
            response.endpoint = candidate;
            break;
         } catch (const fcl::exceptions::base& error) {
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
   if (negotiated.protocol == builtins::meshsub_v11 || negotiated.protocol == builtins::meshsub_v10) {
      co_await handle_pubsub(session, std::move(negotiated.stream));
      co_return;
   }
   auto handler = handler_for(negotiated.protocol);
   if (!handler) {
      increment_protocol_rejected();
      FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "unsupported negotiated relayed P2P protocol");
   }
   increment_protocol_accepted();
   co_await (*handler)(node::incoming_protocol_stream{
       .session = session->info,
       .protocol = negotiated.protocol,
       .stream = std::move(negotiated.stream),
   });
}

boost::asio::awaitable<void> node::impl::handle_relay_stop(std::shared_ptr<node::impl::session_state> session,
                                                           fcl::p2p::stream stream) {
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
      try {
         trace_relay("stop: accepting yamux stream");
         auto relayed_stream = co_await yamux->async_accept_stream();
         auto relayed = session->info;
         relayed.remote_peer = request.source->id;
         relayed.path = path::kind::relay;
         relayed.relay_peer = session->info.remote_peer;
         auto relayed_session = std::make_shared<session_state>();
         relayed_session->info = std::move(relayed);
         co_await handle_relayed_yamux_stream(relayed_session, fcl::p2p::stream{std::move(relayed_stream)});
      } catch (const fcl::yamux::exceptions::closed&) {
         co_return;
      } catch (const fcl::yamux::exceptions::canceled&) {
         co_return;
      } catch (const std::exception& error) {
         trace_relay(std::string{"stop: relayed stream failed "} + error.what());
         increment_protocol_rejected();
         continue;
      } catch (...) {
         trace_relay("stop: relayed stream failed");
         increment_protocol_rejected();
         continue;
      }
      if (!dcutr_started && options.capabilities.has(capabilities::hole_punching)) {
         dcutr_started = true;
         const auto dcutr_status =
             co_await run_dcutr_initiator(request.source->id, yamux, std::chrono::milliseconds{10'000});
         trace_relay(std::string{"stop: dcutr initiator status="} + std::to_string(static_cast<int>(dcutr_status)));
      }
   }
}

boost::asio::awaitable<void> node::impl::handle_relay_hop(std::shared_ptr<node::impl::session_state> session,
                                                          fcl::p2p::stream stream) {
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
      auto endpoints = local_endpoints_for_control();
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

boost::asio::awaitable<void> node::impl::handle_dcutr(std::shared_ptr<node::impl::session_state> session,
                                                      fcl::p2p::stream stream) {
   trace_relay("dcutr: waiting connect");
   auto buffer = std::vector<std::uint8_t>{};
   auto first = co_await async_read_length_delimited(stream, buffer, hole_punch::options{}.max_message_size);
   trace_relay(std::string{"dcutr: connect bytes="} + std::to_string(first.size()));
   auto request = hole_punch::codec::decode(first);
   if (request.kind != hole_punch::message::message_kind::connect) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "DCUtR expected CONNECT");
   }
   auto observed = local_endpoints_for_control();
   co_await stream.async_write(hole_punch::codec::encode(hole_punch::message{
       .kind = hole_punch::message::message_kind::connect,
       .observed_endpoints = std::move(observed),
   }));
   trace_relay("dcutr: connect sent, waiting sync");
   auto sync_bytes = co_await async_read_length_delimited(stream, buffer, hole_punch::options{}.max_message_size);
   trace_relay(std::string{"dcutr: sync bytes="} + std::to_string(sync_bytes.size()));
   auto sync = hole_punch::codec::decode(sync_bytes);
   if (sync.kind != hole_punch::message::message_kind::sync) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "DCUtR expected SYNC");
   }
   for (const auto& candidate : request.observed_endpoints) {
      trace_relay(std::string{"dcutr inbound: direct candidate "} + candidate.to_string());
      try {
         (void)co_await connect_direct(candidate, node::connect_options{
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
      FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "DHT server mode is disabled");
   }
   auto buffer = std::vector<std::uint8_t>{};
   auto request = dht::codec::decode(
       co_await async_read_length_delimited(stream, buffer, options.limits.dht.max_message_size), options.limits.dht);
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
      increment_dht_response();
      co_await stream.async_close();
      co_return;
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
      FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "rendezvous server mode is disabled");
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
         auto registered_peer = session->info.remote_peer;
         if (!request.register_value->signed_peer_record.empty()) {
            try {
               const auto record = rendezvous::codec::open_peer_record(
                   signed_envelope::decode(request.register_value->signed_peer_record), session->info.remote_peer);
               registered_peer = record.peer;
               endpoints = record.endpoints;
            } catch (const fcl::exceptions::base&) {
               response.status_value = rendezvous::status::invalid_signed_peer_record;
               response.status_text = "rendezvous signed peer record is invalid";
            }
         } else if (options.limits.rendezvous.require_signed_peer_record) {
            response.status_value = rendezvous::status::invalid_signed_peer_record;
            response.status_text = "rendezvous registration requires signed peer record";
         }
         if (response.status_value == rendezvous::status::ok && endpoints.empty()) {
            if (const auto record = store.find(registered_peer)) {
               for (const auto& endpoint : record->endpoints) {
                  auto item = endpoint.endpoint;
                  item.peer = registered_peer;
                  endpoints.push_back(std::move(item));
               }
            }
         }
         if (response.status_value == rendezvous::status::ok) {
            store.upsert_rendezvous(rendezvous::registration{
                .namespace_name = request.register_value->namespace_name,
                .peer = registered_peer,
                .endpoints = std::move(endpoints),
                .signed_peer_record = request.register_value->signed_peer_record,
                .ttl = ttl,
                .expires_at = std::chrono::system_clock::now() + ttl,
            });
            response.ttl = ttl;
            increment_rendezvous_registration();
         }
      }
      co_await stream.async_write(rendezvous::codec::encode(
          rendezvous::message{
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
      co_await stream.async_write(rendezvous::codec::encode(
          rendezvous::message{
              .type = rendezvous::message_type::discover_response,
              .discover_response_value =
                  rendezvous::discover_response{
                      .registrations = std::move(registrations),
                      .cookie = rendezvous::codec::make_cookie(sequence, request.discover_value->namespace_name),
                      .status_value = rendezvous::status::ok,
                  },
          },
          options.limits.rendezvous));
      co_await stream.async_close();
      co_return;
   }

   co_await stream.async_write(rendezvous::codec::encode(
       rendezvous::message{
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

boost::asio::awaitable<void> node::impl::handle_pubsub(std::shared_ptr<node::impl::session_state> session,
                                                       fcl::p2p::stream stream) {
   if (!options.capabilities.has(capabilities::pubsub)) {
      FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "GossipSub is disabled");
   }

   auto buffer = std::vector<std::uint8_t>{};
   while (true) {
      auto payload = std::vector<std::uint8_t>{};
      auto close_after_error = false;
      try {
         payload = co_await async_read_length_delimited(stream, buffer, options.limits.pubsub.limits.max_rpc_size);
      } catch (const fcl::exceptions::base& error) {
         if (is_orderly_stream_close(error)) {
            co_return;
         }
         increment_pubsub_invalid(session->info.remote_peer);
         increment_protocol_rejected();
         close_after_error = true;
      }
      if (close_after_error) {
         co_await stream.async_close();
         co_return;
      }

      auto value = pubsub::rpc{};
      close_after_error = false;
      try {
         value = pubsub::codec::decode(payload, options.limits.pubsub);
      } catch (const fcl::exceptions::base&) {
         increment_pubsub_invalid(session->info.remote_peer);
         increment_protocol_rejected();
         close_after_error = true;
      }
      if (close_after_error) {
         co_await stream.async_close();
         co_return;
      }

      if (!value.subscriptions.empty()) {
         auto announce_back = std::vector<pubsub::subscription>{};
         auto subscription_limit_reached = false;
         {
            auto lock = std::scoped_lock{mutex};
            for (const auto& subscription : value.subscriptions) {
               if (subscription.subscribe) {
                  auto& mesh = pubsub_value.mesh[subscription.subject.value];
                  if (!mesh.contains(session->info.remote_peer) &&
                      mesh.size() >= options.limits.pubsub.limits.max_peers_per_topic) {
                     subscription_limit_reached = true;
                     continue;
                  }
                  pubsub_value.peer_topics[session->info.remote_peer].insert(subscription.subject.value);
                  mesh.insert(session->info.remote_peer);
                  if (pubsub_value.handlers.contains(subscription.subject.value)) {
                     announce_back.push_back(pubsub::subscription{
                         .subscribe = true,
                         .subject = subscription.subject,
                     });
                  }
               } else if (auto topics = pubsub_value.peer_topics.find(session->info.remote_peer);
                          topics != pubsub_value.peer_topics.end()) {
                  topics->second.erase(subscription.subject.value);
                  if (auto mesh = pubsub_value.mesh.find(subscription.subject.value); mesh != pubsub_value.mesh.end()) {
                     mesh->second.erase(session->info.remote_peer);
                  }
               }
            }
         }
         if (subscription_limit_reached) {
            increment_pubsub_invalid(session->info.remote_peer);
            increment_protocol_rejected();
         }
         if (!announce_back.empty()) {
            co_await send_pubsub_rpc(session->info.remote_peer, pubsub::rpc{.subscriptions = std::move(announce_back)});
         }
      }

      if (value.control_value) {
         increment_pubsub_control();
         if (pubsub_control_over_limit(*value.control_value)) {
            increment_pubsub_invalid(session->info.remote_peer);
            increment_protocol_rejected();
            co_await stream.async_close();
            co_return;
         }
         auto missing = std::vector<std::vector<std::uint8_t>>{};
         auto cached = std::vector<pubsub::message>{};
         {
            auto lock = std::scoped_lock{mutex};
            for (const auto& graft : value.control_value->grafts) {
               if (pubsub_value.handlers.contains(graft.subject.value)) {
                  pubsub_value.mesh[graft.subject.value].insert(session->info.remote_peer);
               }
            }
            for (const auto& prune : value.control_value->prunes) {
               if (auto mesh = pubsub_value.mesh.find(prune.subject.value); mesh != pubsub_value.mesh.end()) {
                  mesh->second.erase(session->info.remote_peer);
               }
            }
            for (const auto& ihave : value.control_value->have) {
               if (!pubsub_value.handlers.contains(ihave.subject.value)) {
                  continue;
               }
               for (const auto& id : ihave.message_ids) {
                  if (!pubsub_value.cache.contains(bytes_key(id))) {
                     missing.push_back(id);
                  }
               }
            }
            for (const auto& iwant : value.control_value->want) {
               for (const auto& id : iwant.message_ids) {
                  if (const auto found = pubsub_value.cache.find(bytes_key(id)); found != pubsub_value.cache.end()) {
                     cached.push_back(found->second);
                  }
               }
            }
         }
         if (!missing.empty()) {
            try {
               co_await send_pubsub_rpc(
                   session->info.remote_peer,
                   pubsub::rpc{.control_value =
                                   pubsub::control{.want = std::vector<pubsub::control::iwant>{
                                                       pubsub::control::iwant{.message_ids = std::move(missing)}}}});
            } catch (const fcl::exceptions::base&) {
               increment_protocol_rejected();
            }
         }
         if (!cached.empty()) {
            try {
               co_await send_pubsub_rpc(session->info.remote_peer, pubsub::rpc{.messages = std::move(cached)});
            } catch (const fcl::exceptions::base&) {
               increment_protocol_rejected();
            }
         }
      }

      for (const auto& published : value.messages) {
         increment_pubsub_received();

         auto signature_ok = true;
         const auto signed_message = !published.signature.empty();
         switch (options.limits.pubsub.signatures) {
         case pubsub::signature_policy::strict_sign:
            signature_ok = pubsub::codec::verify_message(published);
            break;
         case pubsub::signature_policy::strict_no_sign:
            signature_ok = !signed_message;
            break;
         case pubsub::signature_policy::lax_sign:
         case pubsub::signature_policy::lax_no_sign:
            signature_ok = !signed_message || pubsub::codec::verify_message(published);
            break;
         }
         if (!signature_ok) {
            increment_pubsub_invalid(session->info.remote_peer);
            continue;
         }

         const auto id = pubsub::codec::message_id(published);
         const auto key = bytes_key(id);
         {
            auto lock = std::scoped_lock{mutex};
            if (pubsub_value.cache.contains(key)) {
               ++pubsub_value.scores[session->info.remote_peer].duplicate_messages;
               ++metrics_value.pubsub_duplicates;
               continue;
            }
            pubsub_value.cache.emplace(key, published);
            pubsub_value.history.push_back(key);
            const auto max_cached = std::max<std::size_t>(
                options.limits.pubsub.limits.history_length * options.limits.pubsub.limits.max_messages, 1);
            while (pubsub_value.history.size() > max_cached) {
               pubsub_value.cache.erase(pubsub_value.history.front());
               pubsub_value.history.pop_front();
            }
         }

         auto result = pubsub::validation_result::accept;
         auto handler = std::optional<pubsub::handler>{};
         {
            auto lock = std::scoped_lock{mutex};
            if (const auto found = pubsub_value.handlers.find(published.subject.value);
                found != pubsub_value.handlers.end()) {
               handler = found->second;
            }
         }
         if (handler) {
            if (!try_begin_pubsub_validation(session->info.remote_peer)) {
               continue;
            }
            try {
               result = co_await (*handler)(pubsub::event{
                   .source = published.from.value_or(session->info.remote_peer),
                   .value = published,
               });
               finish_pubsub_validation(session->info.remote_peer);
            } catch (...) {
               finish_pubsub_validation(session->info.remote_peer);
               throw;
            }
         }
         if (result == pubsub::validation_result::reject) {
            increment_pubsub_invalid(session->info.remote_peer);
            continue;
         }
         if (result == pubsub::validation_result::accept && handler) {
            increment_pubsub_delivered();
         }

         auto should_forward = false;
         {
            auto lock = std::scoped_lock{mutex};
            should_forward = pubsub_value.handlers.contains(published.subject.value) ||
                             pubsub_value.mesh.contains(published.subject.value);
         }
         if (!should_forward) {
            continue;
         }
         for (const auto& peer : pubsub_candidate_peers(published.subject.value, session->info.remote_peer)) {
            try {
               co_await send_pubsub_rpc(peer, pubsub::rpc{.messages = std::vector<pubsub::message>{published}});
            } catch (const fcl::exceptions::base&) {
               increment_protocol_rejected();
            }
         }
      }
   }
}

boost::asio::awaitable<bool> node::impl::wait_for_direct_session(const peer_id& peer,
                                                                 std::chrono::milliseconds timeout) {
   const auto started = std::chrono::steady_clock::now();
   while (std::chrono::steady_clock::now() - started < timeout) {
      if (auto existing = session_for(peer); existing && existing->info.path == path::kind::direct) {
         co_return true;
      }
      auto remaining =
          timeout - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
      if (remaining <= std::chrono::milliseconds{0}) {
         break;
      }
      auto timer = asio::steady_timer{runtime.context()};
      timer.expires_after(std::min(remaining, std::chrono::milliseconds{50}));
      co_await timer.async_wait(asio::use_awaitable);
   }
   co_return false;
}

boost::asio::awaitable<hole_punch::status> node::impl::run_dcutr_initiator(const peer_id& peer,
                                                                           std::shared_ptr<fcl::yamux::session> yamux,
                                                                           std::chrono::milliseconds timeout) {
   auto observed = local_endpoints_for_control();
   if (observed.empty()) {
      record_hole_punch_result(hole_punch::status::failed);
      co_return hole_punch::status::failed;
   }
   try {
      trace_relay("dcutr initiator: open yamux stream");
      auto stream = fcl::p2p::stream{co_await yamux->async_open_stream()};
      stream = co_await protocol_negotiation::async_select(std::move(stream), builtins::dcutr);
      const auto sent = std::chrono::steady_clock::now();
      co_await stream.async_write(hole_punch::codec::encode(hole_punch::message{
          .kind = hole_punch::message::message_kind::connect,
          .observed_endpoints = observed,
      }));
      auto dcutr_buffer = std::vector<std::uint8_t>{};
      auto response = hole_punch::codec::decode(
          co_await async_read_length_delimited(stream, dcutr_buffer, hole_punch::options{}.max_message_size));
      const auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - sent);
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
            (void)co_await connect_direct(candidate, node::connect_options{
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
                                                   std::shared_ptr<fcl::yamux::session> yamux,
                                                   std::chrono::milliseconds timeout) {
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
      try {
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
         } else if (negotiated.protocol == builtins::meshsub_v11 || negotiated.protocol == builtins::meshsub_v10) {
            co_await handle_pubsub(relayed_session, std::move(negotiated.stream));
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
      } catch (const fcl::yamux::exceptions::closed&) {
         break;
      } catch (const fcl::yamux::exceptions::canceled&) {
         break;
      } catch (const std::exception& error) {
         trace_relay(std::string{"relayed yamux wait: stream failed "} + error.what());
         increment_protocol_rejected();
         continue;
      } catch (...) {
         trace_relay("relayed yamux wait: stream failed");
         increment_protocol_rejected();
         continue;
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

boost::asio::awaitable<void> node::impl::handle_peer_exchange(fcl::p2p::stream stream, std::uint64_t request_id) {
   auto endpoints = std::vector<peer_exchange_message::endpoint_record>{};
   for (const auto& endpoint : local_endpoints_for_control()) {
      endpoints.push_back(peer_exchange_message::endpoint_record{
          .peer = local,
          .endpoint = endpoint,
          .capabilities = options.capabilities,
      });
      if (endpoints.size() >= options.limits.max_peer_exchange_records) {
         break;
      }
   }
   const auto snapshot = store.snapshot();
   for (const auto& record : snapshot) {
      for (const auto& endpoint : record.endpoints) {
         if (endpoints.size() >= options.limits.max_peer_exchange_records) {
            break;
         }
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
   co_await peer_exchange_codec::async_write(stream,
                                             peer_exchange_message{
                                                 .kind = peer_exchange_message::type::peer_exchange_response,
                                                 .request_id = request_id,
                                                 .peer = local,
                                                 .endpoints = std::move(endpoints),
                                             },
                                             codec_for(options));
   co_await stream.async_close();
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
                auto chunk = co_await pair->left.async_read_chunk();
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
                co_await pair->right.async_write(std::move(chunk));
             }
          } catch (const fcl::exceptions::base& error) {
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
                auto chunk = co_await pair->right.async_read_chunk();
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
                co_await pair->left.async_write(std::move(chunk));
             }
          } catch (const fcl::exceptions::base& error) {
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

boost::asio::awaitable<hole_punch::status>
node::impl::attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer, std::chrono::milliseconds timeout) {
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
      FCL_THROW_EXCEPTION(exceptions::relay_not_available, "P2P hole punching requires a relay peer");
   }
   auto observed = local_endpoints_for_control();
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
