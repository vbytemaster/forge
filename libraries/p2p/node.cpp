module;

#include <forge/exceptions/macros.hpp>

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
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

module forge.p2p.node;

import forge.crypto.chacha20_poly1305;
import forge.crypto.der;
import forge.crypto.ed25519;
import forge.crypto.hmac;
import forge.crypto.pem;
import forge.crypto.asymmetric;
import forge.p2p.dht;
import forge.p2p.diagnostics;
import forge.p2p.discovery;
import forge.p2p.endpoint;
import forge.p2p.envelope;
import forge.p2p.hole_punch;
import forge.p2p.identify;
import forge.p2p.exceptions;
import forge.p2p.message;
import forge.p2p.negotiation;
import forge.p2p.pubsub;
import forge.p2p.reachability;
import forge.p2p.rendezvous;
import forge.p2p.resource_manager;
import forge.p2p.scoring;
import forge.p2p.stream;
import forge.crypto.random;
import forge.crypto.rsa;
import forge.crypto.sha256;
import forge.crypto.x25519;
import forge.multiformats.types;
import forge.multiformats.varint;
import forge.multiformats.exceptions;
import forge.transport.session;
import forge.transport.stream;
import forge.yamux.session;

#include "details/node_impl.hxx"
#include "details/dht_query.hxx"
#include "details/protocol_capabilities.hxx"

namespace forge::p2p {

namespace {

[[nodiscard]] bool supports(const peer_store::record& record, std::uint64_t capability) noexcept {
   return record.capabilities.has(capability);
}

[[nodiscard]] bool queryable(const peer_store::record& record) noexcept {
   return !record.endpoints.empty() &&
          (record.discovery_backoff_until == std::chrono::system_clock::time_point{} ||
           record.discovery_backoff_until <= std::chrono::system_clock::now());
}

[[nodiscard]] std::vector<peer_store::record> discovery_records(std::span<const peer_store::record> records,
                                                                std::uint64_t capability,
                                                                std::size_t limit) {
   auto out = std::vector<peer_store::record>{};
   if (limit == 0) {
      return out;
   }
   for (const auto& record : records) {
      if (!valid_peer_id(record.peer) || !supports(record, capability) || !queryable(record)) {
         continue;
      }
      out.push_back(record);
   }
   std::stable_sort(out.begin(), out.end(), [](const auto& left, const auto& right) {
      if (left.score != right.score) {
         return left.score > right.score;
      }
      return left.peer.to_string() < right.peer.to_string();
   });
   if (out.size() > limit) {
      out.resize(limit);
   }
   return out;
}

[[nodiscard]] dht::peer dht_peer_from_record(const peer_store::record& record) {
   auto endpoints = std::vector<endpoint>{};
   endpoints.reserve(record.endpoints.size());
   for (const auto& item : record.endpoints) {
      auto endpoint = item.endpoint;
      endpoint.peer = record.peer;
      endpoints.push_back(std::move(endpoint));
   }
   return dht::peer{.id = record.peer, .endpoints = std::move(endpoints), .connection = dht::connection_type::can_connect};
}

[[nodiscard]] host_addresses::learning_context third_party_discovery_context() {
   return host_addresses::learning_context{.source = host_addresses::source_kind::third_party};
}

[[nodiscard]] dht::peer sanitize_discovered_peer(dht::peer value, host_addresses::learning_context context) {
   value.endpoints = host_addresses::sanitize_discovered_endpoints(std::move(value.endpoints), value.id, context);
   return value;
}

[[nodiscard]] bool has_usable_endpoint(const dht::peer& value) noexcept {
   return !value.endpoints.empty();
}

void append_result(std::vector<discovery::result>& out, const peer_store::record& record, discovery::source source,
                   std::chrono::system_clock::time_point expires_at, std::size_t limit) {
   if (record.peer.value.empty() || out.size() >= limit) {
      return;
   }
   const auto exists = std::ranges::any_of(out, [&](const auto& current) {
      return current.peer == record.peer;
   });
   if (exists) {
      return;
   }
   auto endpoints = std::vector<endpoint>{};
   endpoints.reserve(record.endpoints.size());
   for (const auto& item : record.endpoints) {
      endpoints.push_back(item.endpoint);
   }
   out.push_back(discovery::result{
       .peer = record.peer,
       .endpoints = std::move(endpoints),
       .capabilities = record.capabilities,
       .discovered_by = source,
       .preferred_path = path::kind::direct,
       .expires_at = expires_at,
       .score = record.score,
   });
}

[[nodiscard]] std::chrono::milliseconds elapsed_since(std::chrono::steady_clock::time_point now,
                                                       std::chrono::steady_clock::time_point started) {
   if (started == std::chrono::steady_clock::time_point{} || now <= started) {
      return std::chrono::milliseconds{0};
   }
   return std::chrono::duration_cast<std::chrono::milliseconds>(now - started);
}

[[nodiscard]] diagnostics::session_direction diagnostics_direction(connection_manager::direction value) noexcept {
   return value == connection_manager::direction::inbound ? diagnostics::session_direction::inbound
                                                          : diagnostics::session_direction::outbound;
}

[[nodiscard]] std::vector<diagnostics::endpoint_record>
diagnostics_endpoints(std::span<const peer_store::endpoint_record> records, std::size_t limit) {
   auto out = std::vector<diagnostics::endpoint_record>{};
   out.reserve(std::min(records.size(), limit));
   for (const auto& record : records) {
      if (out.size() >= limit) {
         break;
      }
      out.push_back(diagnostics::endpoint_record{
          .endpoint = record.endpoint,
          .kind = record.kind,
          .relay_peer = record.relay_peer,
          .successes = record.successes,
          .failures = record.failures,
          .last_latency = record.last_latency,
          .backoff_until = record.backoff_until,
          .score = record.score,
      });
   }
   return out;
}

[[nodiscard]] std::vector<diagnostics::relay_reservation>
diagnostics_relays(std::span<const peer_store::relay_record> records, std::size_t limit, std::size_t endpoint_limit) {
   auto out = std::vector<diagnostics::relay_reservation>{};
   out.reserve(std::min(records.size(), limit));
   for (const auto& record : records) {
      if (out.size() >= limit) {
         break;
      }
      auto endpoints = record.endpoints;
      if (endpoints.size() > endpoint_limit) {
         endpoints.resize(endpoint_limit);
      }
      out.push_back(diagnostics::relay_reservation{
          .relay = record.relay,
          .reservation_id = record.reservation_id,
          .expires_at = record.expires_at,
          .endpoints = std::move(endpoints),
          .successes = record.successes,
          .failures = record.failures,
          .last_latency = record.last_latency,
          .score = record.score,
      });
   }
   return out;
}

[[nodiscard]] diagnostics::peer diagnostics_peer(const peer_store::record& record, const diagnostics::options& options,
                                                 bool protected_peer) {
   auto protocols = record.protocols;
   if (protocols.size() > options.max_protocols_per_peer) {
      protocols.resize(options.max_protocols_per_peer);
   }
   return diagnostics::peer{
       .peer = record.peer,
       .capabilities = record.capabilities,
       .discovered_by = record.discovered_by,
       .protocol_version = record.protocol_version,
       .agent_version = record.agent_version,
       .protocols = std::move(protocols),
       .endpoints = diagnostics_endpoints(record.endpoints, options.max_endpoints_per_peer),
       .relay_reservations =
           diagnostics_relays(record.relay_reservations, options.max_relay_reservations_per_peer,
                              options.max_endpoints_per_peer),
       .reachability = record.reachability,
       .observed_endpoint = record.observed_endpoint,
       .reachability_expires_at = record.reachability_expires_at,
       .discovered_at = record.discovered_at,
       .discovery_expires_at = record.discovery_expires_at,
       .discovery_backoff_until = record.discovery_backoff_until,
       .successes = record.successes,
       .failures = record.failures,
       .last_latency = record.last_latency,
       .score = record.score,
       .protected_peer = protected_peer,
   };
}

[[nodiscard]] pubsub::snapshot diagnostics_pubsub(const auto& impl) {
   auto mesh_edges = std::size_t{};
   for (const auto& [_, peers] : impl.pubsub_value.mesh) {
      mesh_edges += peers.size();
   }
   return pubsub::snapshot{
       .topics = impl.pubsub_value.handlers.size(),
       .peers = impl.pubsub_value.peer_topics.size(),
       .mesh_edges = mesh_edges,
       .cached_messages = impl.pubsub_value.cache.size(),
       .messages_published = impl.metrics_value.pubsub_messages_published,
       .messages_received = impl.metrics_value.pubsub_messages_received,
       .messages_delivered = impl.metrics_value.pubsub_messages_delivered,
       .duplicates = impl.metrics_value.pubsub_duplicates,
       .invalid_messages = impl.metrics_value.pubsub_invalid_messages,
       .control_messages = impl.metrics_value.pubsub_control_messages,
   };
}

boost::asio::awaitable<std::optional<identify::document>>
identify_peer(auto self, const peer_id& peer, discovery::source source, std::chrono::milliseconds timeout) {
   try {
      auto stream = co_await self->open_protocol_direct(peer, builtins::identify, timeout);
      auto buffer = std::vector<std::uint8_t>{};
      auto payload = unwrap_length_delimited(
          co_await async_read_length_delimited(stream, buffer, self->options.limits.max_peer_exchange_message_size),
          self->options.limits.max_peer_exchange_message_size);
      auto document = identify::decode(payload);
      self->learn_from_identify(peer, document);
      auto record = self->store.find(peer).value_or(peer_store::record{.peer = peer});
      record.discovered_by = source;
      record.discovered_at = std::chrono::system_clock::now();
      record.discovery_expires_at = record.discovered_at + self->options.limits.discovery.refresh_interval;
      record.capabilities.bits |= capabilities_for(document.protocols).bits;
      self->store.upsert(std::move(record));
      co_await stream.async_close();
      co_return document;
   } catch (const forge::exceptions::base&) {
      self->store.mark_failure(peer);
      co_return std::nullopt;
   }
}

[[nodiscard]] std::vector<endpoint> endpoints_from_registration(const rendezvous::registration& registration) {
   if (registration.signed_peer_record.empty()) {
      return registration.endpoints;
   }
   try {
      const auto record =
          rendezvous::codec::open_peer_record(signed_envelope::decode(registration.signed_peer_record),
                                              registration.peer);
      return record.endpoints;
   } catch (const forge::exceptions::base&) {
      return {};
   }
}

[[nodiscard]] std::optional<rendezvous::registration>
sanitize_discovered_registration(rendezvous::registration registration, host_addresses::learning_context context) {
   const auto original_endpoints = endpoints_from_registration(registration);
   if (original_endpoints.empty()) {
      if (!registration.signed_peer_record.empty()) {
         return std::nullopt;
      }
      return registration;
   }
   auto sanitized =
       host_addresses::sanitize_discovered_endpoints(original_endpoints, registration.peer, std::move(context));
   if (sanitized.empty()) {
      return std::nullopt;
   }
   if (registration.signed_peer_record.empty() || sanitized.size() != original_endpoints.size()) {
      registration.signed_peer_record.clear();
   } else {
      for (auto index = std::size_t{0}; index < sanitized.size(); ++index) {
         if (sanitized[index].to_string() != original_endpoints[index].to_string()) {
            registration.signed_peer_record.clear();
            break;
         }
      }
   }
   registration.endpoints = std::move(sanitized);
   return registration;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
make_local_rendezvous_record(const auto& self, std::uint64_t sequence) {
   if (self.options.public_key.empty() || self.options.private_key_pem.empty()) {
      return std::nullopt;
   }
   auto endpoints = self.local_endpoints_for_control();
   if (endpoints.empty()) {
      return std::nullopt;
   }
   return rendezvous::codec::seal_peer_record(
              rendezvous::peer_record{
                  .peer = self.local,
                  .endpoints = std::move(endpoints),
                  .sequence = sequence,
              },
              decode_public_key(self.options.public_key), private_key_from_pem(self.options.private_key_pem))
       .encode();
}

} // namespace

node::node(forge::asio::runtime& runtime, node::options options) {
   validate(options);
   impl_ = std::make_shared<impl>(runtime, std::move(options));
}

node::~node() = default;
node::node(node&&) noexcept = default;
node& node::operator=(node&&) noexcept = default;

const peer_id& node::local_peer() const noexcept {
   return impl_->local;
}

std::optional<forge::p2p::endpoint> node::local_endpoint() const {
   auto endpoints = local_endpoints();
   if (endpoints.empty()) {
      return std::nullopt;
   }
   return endpoints.front();
}

std::vector<forge::p2p::endpoint> node::local_endpoints() const {
   return impl_->local_endpoints_for_control();
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

forge::p2p::diagnostics::snapshot node::diagnostics(forge::p2p::diagnostics::options options) const {
   auto lock = std::scoped_lock{impl_->mutex};
   auto out = forge::p2p::diagnostics::snapshot{};
   out.network = forge::p2p::diagnostics::network_state{
      .local_peer = impl_->local,
      .local_endpoints = impl_->local_endpoints_for_control_locked(),
      .stopped = impl_->stopped,
   };
   out.metrics = impl_->metrics_value;
   out.metrics.active_sessions = impl_->sessions.size();
   out.metrics.active_relay_reservations = impl_->inbound_relay_reservations.size();
   out.metrics.stopped = impl_->stopped;
   out.resources = impl_->resources.current();
   out.pubsub = diagnostics_pubsub(*impl_);

   auto connection_snapshot = impl_->connections.current(options.max_sessions);
   out.connections = forge::p2p::diagnostics::connection_state{
      .active_sessions = connection_snapshot.active_sessions,
      .protected_peers = std::move(connection_snapshot.protected_peers),
   };

   const auto now = std::chrono::steady_clock::now();
   out.sessions.reserve(connection_snapshot.sessions.size());
   for (const auto& record : connection_snapshot.sessions) {
      const auto found = impl_->sessions.find(record.id);
      if (found == impl_->sessions.end()) {
         continue;
      }
      const auto& session = *found->second;
      out.sessions.push_back(forge::p2p::diagnostics::session{
          .id = session.id,
          .remote_peer = session.info.remote_peer,
          .capabilities = session.info.capabilities,
          .path = session.info.path,
          .relay_peer = session.info.relay_peer,
          .direct_endpoint = session.direct_endpoint,
          .remote_endpoint = session.remote_endpoint,
          .direction = diagnostics_direction(record.direction),
          .age = elapsed_since(now, record.opened_at),
          .idle = elapsed_since(now, record.last_used_at),
          .closed = session.closed,
          .protected_peer = impl_->connections.is_protected(session.info.remote_peer),
      });
   }

   if (options.max_peers > 0) {
      const auto records = impl_->store.snapshot();
      out.peers.reserve(std::min(options.max_peers, records.size()));
      for (const auto& record : records) {
         if (out.peers.size() >= options.max_peers) {
            break;
         }
         out.peers.push_back(diagnostics_peer(record, options, impl_->connections.is_protected(record.peer)));
      }
   }
   return out;
}

peer_store& node::peers() noexcept {
   return impl_->store;
}

const peer_store& node::peers() const noexcept {
   return impl_->store;
}

void node::protect_peer(peer_id peer, std::string tag) {
   auto lock = std::scoped_lock{impl_->mutex};
   impl_->connections.protect(peer, std::move(tag));
}

bool node::unprotect_peer(peer_id peer, std::string tag) {
   auto lock = std::scoped_lock{impl_->mutex};
   return impl_->connections.unprotect(peer, tag);
}

bool node::is_peer_protected(const peer_id& peer) const {
   auto lock = std::scoped_lock{impl_->mutex};
   return impl_->connections.is_protected(peer);
}

void node::register_protocol_handler(protocol_id protocol, node::protocol_handler handler) {
   if (protocol.value.empty() || !handler) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P protocol handler requires protocol id and handler");
   }
   auto lock = std::scoped_lock{impl_->mutex};
   if (impl_->handlers.size() >= impl_->options.limits.max_protocol_handlers) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "P2P max protocol handlers reached");
   }
   const auto [_, inserted] = impl_->handlers.emplace(std::move(protocol), std::move(handler));
   if (!inserted) {
      FORGE_THROW_EXCEPTION(exceptions::duplicate_protocol, "duplicate P2P protocol handler");
   }
}

boost::asio::awaitable<void> node::async_listen(forge::p2p::endpoint endpoint) {
   auto self = impl_;
   auto local_endpoint = forge::p2p::endpoint{};
   {
      auto lock = std::scoped_lock{self->mutex};
      if (self->stopped) {
         FORGE_THROW_EXCEPTION(exceptions::closed, "P2P node is stopped");
      }
      local_endpoint = self->direct_registry.listen(std::move(endpoint));
   }
   self->launch_accept_loop(std::move(local_endpoint));
   self->launch_pubsub_heartbeat();
   self->launch_relay_discovery_maintenance();
   co_return;
}

boost::asio::awaitable<node::session_info> node::async_connect(forge::p2p::endpoint endpoint) {
   return async_connect(std::move(endpoint), connect_options{});
}

boost::asio::awaitable<node::session_info> node::async_connect(forge::p2p::endpoint endpoint,
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

boost::asio::awaitable<reachability::state> node::async_probe_reachability(peer_id observer) {
   auto self = impl_;
   auto endpoints = self->local_endpoints_for_control();
   if (endpoints.empty()) {
      co_return reachability::state::private_network;
   }
   const auto nonce = random_nonce();
   try {
      self->remember_autonat_v2_nonce(observer, nonce);
      auto stream = co_await self->open_protocol_direct(observer, builtins::autonat_v2_dial_request,
                                                        node::open_options{}.timeout);
      co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
          .type = reachability::v2::message::kind::dial_request,
          .dial_request =
              reachability::v2::dial_request{
                  .endpoints = endpoints,
                  .nonce = nonce,
              },
      }));
      auto state = reachability::state::private_network;
      auto observed = std::optional<forge::p2p::endpoint>{};
      auto buffer = std::vector<std::uint8_t>{};
      for (auto step = 0U; step != 8U; ++step) {
         auto message = reachability::codec::decode_v2(
             co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
         if (message.type == reachability::v2::message::kind::dial_data_request && message.dial_data_request) {
            auto remaining = message.dial_data_request->bytes;
            while (remaining > 0) {
               const auto chunk_size = static_cast<std::size_t>(
                   std::min<std::uint64_t>(remaining, reachability::options{}.max_data_response_size));
               co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
                   .type = reachability::v2::message::kind::dial_data_response,
                   .dial_data_response =
                       reachability::v2::dial_data_response{
                           .data = std::vector<std::uint8_t>(chunk_size, 0x61),
                       },
               }));
               remaining -= chunk_size;
            }
            continue;
         }
         if (message.type != reachability::v2::message::kind::dial_response || !message.dial_response) {
            FORGE_THROW_EXCEPTION(exceptions::protocol_error, "AutoNAT v2 probe expected dial response");
         }
         if (message.dial_response->status == reachability::v2::response_status::ok &&
             message.dial_response->dial_status == reachability::v2::dial_status::ok) {
            state = reachability::state::publicly_reachable;
            if (message.dial_response->index < endpoints.size()) {
               observed = endpoints[message.dial_response->index];
            }
         } else if (message.dial_response->status == reachability::v2::response_status::dial_refused ||
                    message.dial_response->dial_status == reachability::v2::dial_status::dial_back_error) {
            state = reachability::state::blocked;
         }
         self->forget_autonat_v2_nonce(observer);
         self->increment_reachability_check(state);
         self->store.mark_reachability(self->local, state, observed);
         co_return state;
      }
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "AutoNAT v2 probe exceeded message exchange limit");
   } catch (const forge::exceptions::base& error) {
      self->forget_autonat_v2_nonce(observer);
      if (p2p_code(error) != exceptions::code::unsupported_protocol) {
         throw;
      }
   }
   auto stream = co_await self->open_protocol_direct(observer, builtins::autonat_v1, node::open_options{}.timeout);
   co_await stream.async_write(reachability::codec::encode_v1(reachability::message{
       .kind = reachability::message::message_kind::dial,
       .peer =
           reachability::peer_info{
               .peer = self->local,
               .endpoints = std::move(endpoints),
           },
   }));
   auto response = reachability::codec::decode_v1(co_await stream.async_read());
   if (response.kind != reachability::message::message_kind::dial_response || !response.response) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "AutoNAT probe expected dial response");
   }
   auto state = reachability::state::private_network;
   if (response.response->status == reachability::dial_status::ok) {
      state = reachability::state::publicly_reachable;
   } else if (response.response->status == reachability::dial_status::dial_refused) {
      state = reachability::state::blocked;
   }
   self->increment_reachability_check(state);
   self->store.mark_reachability(
       self->local, state,
       response.response->endpoint ? std::make_optional(*response.response->endpoint) : std::nullopt);
   co_return state;
}

boost::asio::awaitable<relay::reservation::info> node::async_reserve_relay(peer_id relay_peer) {
   return async_reserve_relay(std::move(relay_peer), relay::reservation::options{});
}

boost::asio::awaitable<relay::reservation::info> node::async_reserve_relay(peer_id relay_peer,
                                                                           relay::reservation::options options) {
   auto self = impl_;
   co_return co_await self->request_relay_reservation(relay_peer, options, node::connect_options{}.timeout);
}

boost::asio::awaitable<std::vector<relay::reservation::info>> node::async_refresh_relay_candidates() {
   auto self = impl_;
   co_return co_await self->refresh_relay_candidates(std::nullopt, self->options.limits.discovery.query_timeout);
}

boost::asio::awaitable<std::vector<discovery::result>> node::async_refresh_discovery() {
   auto self = impl_;
   validate_operation_timeout(self->options.limits.discovery.query_timeout, "P2P discovery refresh timeout");
   if (!self->options.limits.discovery.enabled) {
      co_return std::vector<discovery::result>{};
   }

   const auto now = std::chrono::system_clock::now();
   const auto expires_at = now + self->options.limits.discovery.refresh_interval;
   auto out = std::vector<discovery::result>{};
   out.reserve(self->options.limits.discovery.max_results);

   if (self->options.limits.discovery.dht_enabled) {
      auto seeds = std::vector<dht::peer>{};
      const auto snapshot = self->store.snapshot();
      for (const auto& record :
           discovery_records(snapshot, capabilities::dht, self->options.limits.discovery.max_parallel_queries)) {
         if (record.peer == self->local) {
            continue;
         }
         seeds.push_back(dht_peer_from_record(record));
      }
      if (!seeds.empty()) {
         const auto target = make_dht_key(self->local);
         auto lookup = co_await dht_query::run(
             dht_query::request{
                 .target = target,
                 .options = self->options.limits.dht,
                 .seeds = std::move(seeds),
             },
             [self, target](const dht::peer& candidate) -> boost::asio::awaitable<dht::message> {
                auto stream = co_await self->open_protocol_direct(candidate.id, builtins::kad_dht,
                                                                   self->options.limits.discovery.query_timeout);
                co_await stream.async_write(dht::codec::encode(dht::message{
                    .type = dht::message_type::find_node,
                    .key_value = target,
                },
                                                               self->options.limits.dht));
                auto buffer = std::vector<std::uint8_t>{};
                auto response = dht::codec::decode(
                    co_await async_read_length_delimited(stream, buffer, self->options.limits.dht.max_message_size),
                    self->options.limits.dht);
                const auto context = third_party_discovery_context();
                for (auto& closer : response.closer_peers) {
                   closer = sanitize_discovered_peer(std::move(closer), context);
                   if (has_usable_endpoint(closer)) {
                      self->store.upsert_routing_peer(
                          closer, discovery::source::dht,
                          std::chrono::system_clock::now() + self->options.limits.dht.refresh_interval);
                   }
                }
                response.closer_peers.erase(std::remove_if(response.closer_peers.begin(), response.closer_peers.end(),
                                                           [](const auto& peer) { return !has_usable_endpoint(peer); }),
                                            response.closer_peers.end());
                co_await stream.async_close();
                co_return response;
             });
         for (const auto& failed : lookup.failed) {
            self->store.mark_failure(failed);
         }
         for (const auto& peer : lookup.query.closest_peers) {
            if (peer.id == self->local || out.size() >= self->options.limits.discovery.max_results) {
               continue;
            }
            (void)co_await identify_peer(self, peer.id, discovery::source::dht,
                                         self->options.limits.discovery.query_timeout);
            if (const auto record = self->store.find(peer.id)) {
               append_result(out, *record, discovery::source::dht, expires_at, self->options.limits.discovery.max_results);
            }
         }
      }
   }

   if (self->options.limits.discovery.rendezvous_enabled) {
      const auto snapshot = self->store.snapshot();
      for (const auto& record :
           discovery_records(snapshot, capabilities::rendezvous, self->options.limits.discovery.max_parallel_queries)) {
         if (record.peer == self->local) {
            continue;
         }
         for (const auto& namespace_name : self->options.limits.discovery.rendezvous_namespaces) {
            if (namespace_name.empty() || out.size() >= self->options.limits.discovery.max_results) {
               continue;
            }
            if (auto signed_record = make_local_rendezvous_record(*self, random_nonce())) {
               try {
                  (void)co_await async_rendezvous_register(record.peer, rendezvous::register_request{
                                                                            .namespace_name = namespace_name,
                                                                            .signed_peer_record = std::move(*signed_record),
                                                                            .ttl = self->options.limits.rendezvous.default_ttl,
                                                                        });
               } catch (const forge::exceptions::base&) {
                  self->store.mark_failure(record.peer);
               }
            }

            auto cookie = std::vector<std::uint8_t>{};
            {
               auto lock = std::scoped_lock{self->mutex};
               const auto it = self->discovery_value.rendezvous_cookies.find({record.peer, namespace_name});
               if (it != self->discovery_value.rendezvous_cookies.end()) {
                  cookie = it->second;
               }
            }

            try {
               auto response = co_await async_rendezvous_discover(record.peer, rendezvous::discover_request{
                                                                                   .namespace_name = namespace_name,
                                                                                   .limit =
                                                                                       self->options.limits.discovery.max_results,
                                                                                   .cookie = std::move(cookie),
                                                                               });
               {
                  auto lock = std::scoped_lock{self->mutex};
                  self->discovery_value.rendezvous_cookies[{record.peer, namespace_name}] = response.cookie;
               }
               for (const auto& registration : response.registrations) {
                  if (registration.peer == self->local || out.size() >= self->options.limits.discovery.max_results) {
                     continue;
                  }
                  auto sanitized = sanitize_discovered_registration(registration, third_party_discovery_context());
                  if (!sanitized) {
                     continue;
                  }
                  for (const auto& endpoint : sanitized->endpoints) {
                     self->store.learn_endpoint(registration.peer, endpoint);
                  }
                  (void)co_await identify_peer(self, registration.peer, discovery::source::rendezvous,
                                               self->options.limits.discovery.query_timeout);
                  if (const auto learned = self->store.find(registration.peer)) {
                     append_result(out, *learned, discovery::source::rendezvous, registration.expires_at,
                                   self->options.limits.discovery.max_results);
                  }
               }
            } catch (const forge::exceptions::base&) {
               self->store.mark_failure(record.peer);
            }
         }
      }
   }

   co_return out;
}

boost::asio::awaitable<void> node::async_cancel_relay(peer_id relay_peer) {
   auto self = impl_;
   {
      auto lock = std::scoped_lock{self->mutex};
      self->cleanup_expired_relay_reservations_locked();
      const auto it = self->outbound_relay_reservations.find(relay_peer);
      if (it == self->outbound_relay_reservations.end()) {
         co_return;
      }
      self->outbound_relay_reservations.erase(it);
   }
}

boost::asio::awaitable<dht::query_result> node::async_find_peer(peer_id peer) {
   auto self = impl_;
   auto target = make_dht_key(peer);
   auto result = dht::query_result{.target = target};
   for (const auto& candidate : self->store.closest_routing_peers(target, self->options.limits.dht.replication)) {
      if (candidate.id == peer) {
         result.closest_peers.push_back(candidate);
         result.complete = true;
         co_return result;
      }
   }
   auto lookup = co_await dht_query::run(
       dht_query::request{
           .target = target,
           .target_peer = peer,
           .options = self->options.limits.dht,
           .seeds = self->store.closest_routing_peers(target, self->options.limits.dht.alpha),
       },
       [self, target](const dht::peer& candidate) -> boost::asio::awaitable<dht::message> {
          auto stream =
              co_await self->open_protocol_direct(candidate.id, builtins::kad_dht, self->options.limits.dht.query_timeout);
          co_await stream.async_write(dht::codec::encode(dht::message{
              .type = dht::message_type::find_node,
              .key_value = target,
          },
                                                         self->options.limits.dht));
          auto buffer = std::vector<std::uint8_t>{};
          auto response = dht::codec::decode(
              co_await async_read_length_delimited(stream, buffer, self->options.limits.dht.max_message_size),
              self->options.limits.dht);
          const auto context = third_party_discovery_context();
          for (auto& closer : response.closer_peers) {
             closer = sanitize_discovered_peer(std::move(closer), context);
             if (has_usable_endpoint(closer)) {
                self->store.upsert_routing_peer(
                    closer, discovery::source::dht,
                    std::chrono::system_clock::now() + self->options.limits.dht.refresh_interval);
             }
          }
          response.closer_peers.erase(std::remove_if(response.closer_peers.begin(), response.closer_peers.end(),
                                                     [](const auto& peer) { return !has_usable_endpoint(peer); }),
                                      response.closer_peers.end());
          co_await stream.async_close();
          co_return response;
       });
   for (const auto& failed : lookup.failed) {
      self->store.mark_failure(failed);
   }
   co_return lookup.query;
}

boost::asio::awaitable<void> node::async_provide(dht::key key) {
   auto self = impl_;
   auto endpoints = self->local_endpoints_for_control();
   auto provider = dht::peer{.id = self->local, .endpoints = endpoints, .connection = dht::connection_type::connected};
   self->store.upsert_provider(peer_store::provider_record{
       .key = key,
       .provider = provider,
       .discovered_by = discovery::source::dht,
       .expires_at = std::chrono::system_clock::now() + self->options.limits.dht.provider_record_ttl,
   });
   auto lookup = co_await dht_query::run(
       dht_query::request{
           .target = key,
           .options = self->options.limits.dht,
           .seeds = self->store.closest_routing_peers(key, self->options.limits.dht.alpha),
       },
       [self, key](const dht::peer& candidate) -> boost::asio::awaitable<dht::message> {
          auto stream =
              co_await self->open_protocol_direct(candidate.id, builtins::kad_dht, self->options.limits.dht.query_timeout);
          co_await stream.async_write(dht::codec::encode(dht::message{
              .type = dht::message_type::find_node,
              .key_value = key,
          },
                                                         self->options.limits.dht));
          auto buffer = std::vector<std::uint8_t>{};
          auto response = dht::codec::decode(
              co_await async_read_length_delimited(stream, buffer, self->options.limits.dht.max_message_size),
              self->options.limits.dht);
          const auto context = third_party_discovery_context();
          for (auto& closer : response.closer_peers) {
             closer = sanitize_discovered_peer(std::move(closer), context);
             if (has_usable_endpoint(closer)) {
                self->store.upsert_routing_peer(
                    closer, discovery::source::dht,
                    std::chrono::system_clock::now() + self->options.limits.dht.refresh_interval);
             }
          }
          response.closer_peers.erase(std::remove_if(response.closer_peers.begin(), response.closer_peers.end(),
                                                     [](const auto& peer) { return !has_usable_endpoint(peer); }),
                                      response.closer_peers.end());
          co_await stream.async_close();
          co_return response;
       });
   for (const auto& failed : lookup.failed) {
      self->store.mark_failure(failed);
   }
   auto candidates = lookup.query.closest_peers;
   if (candidates.empty()) {
      candidates = self->store.closest_routing_peers(key, self->options.limits.dht.replication);
   }
   for (const auto& candidate : candidates) {
      try {
         auto stream = co_await self->open_protocol_direct(candidate.id, builtins::kad_dht,
                                                           self->options.limits.dht.query_timeout);
         co_await stream.async_write(dht::codec::encode(dht::message{
             .type = dht::message_type::add_provider,
             .key_value = key,
             .provider_peers = std::vector<dht::peer>{provider},
         },
                                                        self->options.limits.dht));
         co_await stream.async_close();
      } catch (const forge::exceptions::base&) {
         self->store.mark_failure(candidate.id);
      }
   }
}

boost::asio::awaitable<std::vector<dht::peer>> node::async_find_providers(dht::key key) {
   auto self = impl_;
   auto out = std::vector<dht::peer>{};
   for (const auto& provider : self->store.find_providers(key)) {
      out.push_back(provider.provider);
   }
   if (!out.empty()) {
      co_return out;
   }
   auto lookup = co_await dht_query::run(
       dht_query::request{
           .target = key,
           .options = self->options.limits.dht,
           .seeds = self->store.closest_routing_peers(key, self->options.limits.dht.alpha),
       },
       [self, key](const dht::peer& candidate) -> boost::asio::awaitable<dht::message> {
          auto stream =
              co_await self->open_protocol_direct(candidate.id, builtins::kad_dht, self->options.limits.dht.query_timeout);
          co_await stream.async_write(dht::codec::encode(dht::message{
              .type = dht::message_type::get_providers,
              .key_value = key,
          },
                                                         self->options.limits.dht));
          auto buffer = std::vector<std::uint8_t>{};
          auto response = dht::codec::decode(
              co_await async_read_length_delimited(stream, buffer, self->options.limits.dht.max_message_size),
              self->options.limits.dht);
          const auto context = third_party_discovery_context();
          for (auto& provider : response.provider_peers) {
             provider = sanitize_discovered_peer(std::move(provider), context);
             if (has_usable_endpoint(provider)) {
                self->store.upsert_provider(peer_store::provider_record{
                    .key = key,
                    .provider = provider,
                    .discovered_by = discovery::source::dht,
                    .expires_at = std::chrono::system_clock::now() + self->options.limits.dht.provider_record_ttl,
                });
             }
          }
          for (auto& closer : response.closer_peers) {
             closer = sanitize_discovered_peer(std::move(closer), context);
             if (has_usable_endpoint(closer)) {
                self->store.upsert_routing_peer(
                    closer, discovery::source::dht,
                    std::chrono::system_clock::now() + self->options.limits.dht.refresh_interval);
             }
          }
          response.closer_peers.erase(std::remove_if(response.closer_peers.begin(), response.closer_peers.end(),
                                                     [](const auto& peer) { return !has_usable_endpoint(peer); }),
                                      response.closer_peers.end());
          response.provider_peers.erase(std::remove_if(response.provider_peers.begin(), response.provider_peers.end(),
                                                       [](const auto& peer) { return !has_usable_endpoint(peer); }),
                                        response.provider_peers.end());
          co_await stream.async_close();
          co_return response;
       });
   for (const auto& failed : lookup.failed) {
      self->store.mark_failure(failed);
   }
   co_return lookup.query.provider_peers;
}

boost::asio::awaitable<rendezvous::register_response>
node::async_rendezvous_register(peer_id rendezvous_peer, rendezvous::register_request request) {
   auto self = impl_;
   auto stream = co_await self->open_protocol_direct(rendezvous_peer, builtins::rendezvous,
                                                     self->options.limits.discovery.query_timeout);
   co_await stream.async_write(rendezvous::codec::encode(rendezvous::message{
       .type = rendezvous::message_type::register_peer,
       .register_value = std::move(request),
   },
                                                         self->options.limits.rendezvous));
   auto buffer = std::vector<std::uint8_t>{};
   auto response = rendezvous::codec::decode(
       co_await async_read_length_delimited(stream, buffer, self->options.limits.rendezvous.max_message_size),
       self->options.limits.rendezvous);
   if (response.type != rendezvous::message_type::register_response || !response.register_response_value) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "rendezvous expected register response");
   }
   co_await stream.async_close();
   co_return *response.register_response_value;
}

boost::asio::awaitable<rendezvous::discover_response>
node::async_rendezvous_discover(peer_id rendezvous_peer, rendezvous::discover_request request) {
   auto self = impl_;
   auto stream = co_await self->open_protocol_direct(rendezvous_peer, builtins::rendezvous,
                                                     self->options.limits.discovery.query_timeout);
   co_await stream.async_write(rendezvous::codec::encode(rendezvous::message{
       .type = rendezvous::message_type::discover,
       .discover_value = std::move(request),
   },
                                                         self->options.limits.rendezvous));
   auto buffer = std::vector<std::uint8_t>{};
   auto response = rendezvous::codec::decode(
       co_await async_read_length_delimited(stream, buffer, self->options.limits.rendezvous.max_message_size),
       self->options.limits.rendezvous);
   if (response.type != rendezvous::message_type::discover_response || !response.discover_response_value) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "rendezvous expected discover response");
   }
   auto sanitized_registrations = std::vector<rendezvous::registration>{};
   const auto context = third_party_discovery_context();
   for (auto& registration : response.discover_response_value->registrations) {
      auto sanitized = sanitize_discovered_registration(std::move(registration), context);
      if (!sanitized) {
         continue;
      }
      if (valid_peer_id(sanitized->peer)) {
         self->store.upsert_rendezvous(*sanitized);
      }
      sanitized_registrations.push_back(std::move(*sanitized));
   }
   response.discover_response_value->registrations = std::move(sanitized_registrations);
   co_await stream.async_close();
   co_return *response.discover_response_value;
}

boost::asio::awaitable<pubsub::subscription> node::async_subscribe(pubsub::topic subject, pubsub::handler handler) {
   if (subject.value.empty() || !handler) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub subscription requires topic and handler");
   }
   auto self = impl_;
   auto subscription = pubsub::subscription{.subscribe = true, .subject = std::move(subject)};
   {
      auto lock = std::scoped_lock{self->mutex};
      if (self->pubsub_value.handlers.size() >= self->options.limits.pubsub.limits.max_topics &&
          !self->pubsub_value.handlers.contains(subscription.subject.value)) {
         FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "GossipSub topic limit reached");
      }
      self->pubsub_value.handlers[subscription.subject.value] = std::move(handler);
   }
   auto peers = self->pubsub_candidate_peers(subscription.subject.value);
   for (const auto& peer : peers) {
      try {
         co_await self->send_pubsub_rpc(peer, pubsub::rpc{.subscriptions = std::vector<pubsub::subscription>{subscription}});
      } catch (const forge::exceptions::base&) {
         self->store.mark_failure(peer);
      }
   }
   co_return subscription;
}

boost::asio::awaitable<void> node::async_unsubscribe(pubsub::topic subject) {
   if (subject.value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub unsubscribe requires topic");
   }
   auto self = impl_;
   auto subscription = pubsub::subscription{.subscribe = false, .subject = std::move(subject)};
   {
      auto lock = std::scoped_lock{self->mutex};
      self->pubsub_value.handlers.erase(subscription.subject.value);
      self->pubsub_value.mesh.erase(subscription.subject.value);
   }
   auto peers = self->pubsub_candidate_peers(subscription.subject.value);
   for (const auto& peer : peers) {
      try {
         co_await self->send_pubsub_rpc(peer, pubsub::rpc{.subscriptions = std::vector<pubsub::subscription>{subscription}});
      } catch (const forge::exceptions::base&) {
         self->store.mark_failure(peer);
      }
   }
   co_return;
}

boost::asio::awaitable<pubsub::message> node::async_publish(pubsub::topic subject, std::vector<std::uint8_t> data) {
   co_return co_await async_publish(std::move(subject), std::move(data), pubsub::publish_options{});
}

boost::asio::awaitable<pubsub::message> node::async_publish(pubsub::topic subject, std::vector<std::uint8_t> data,
                                                            pubsub::publish_options publish_options) {
   if (subject.value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub publish requires topic");
   }
   auto self = impl_;
   if (data.size() > self->options.limits.pubsub.limits.max_data_size) {
      FORGE_THROW_EXCEPTION(exceptions::backpressure_rejected, "GossipSub publish exceeds max data size");
   }
   auto value = pubsub::message{
       .data = std::move(data),
       .seqno = self->next_pubsub_seqno(),
       .subject = std::move(subject),
   };
   if (publish_options.sign) {
      const auto key = forge::crypto::pem::read_private_key(self->options.private_key_pem);
      pubsub::codec::sign_message(value, key);
      if (!value.from || *value.from != self->local) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "GossipSub signing key does not match local Peer ID");
      }
   } else if (self->options.limits.pubsub.signatures == pubsub::signature_policy::strict_sign) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub strict-sign node cannot publish unsigned messages");
   }

   const auto id = pubsub::codec::message_id(value);
   {
      auto lock = std::scoped_lock{self->mutex};
      const auto key = bytes_key(id);
      self->pubsub_value.cache[key] = value;
      self->pubsub_value.history.push_back(key);
      while (self->pubsub_value.history.size() >
             self->options.limits.pubsub.limits.history_length * self->options.limits.pubsub.limits.max_messages) {
         self->pubsub_value.cache.erase(self->pubsub_value.history.front());
         self->pubsub_value.history.pop_front();
      }
   }
   self->increment_pubsub_published();

   auto attempted = std::size_t{};
   auto sent = std::size_t{};
   for (const auto& peer : self->pubsub_candidate_peers(value.subject.value)) {
      ++attempted;
      try {
         co_await self->send_pubsub_rpc(peer, pubsub::rpc{.messages = std::vector<pubsub::message>{value}});
         ++sent;
      } catch (const forge::exceptions::base&) {
         self->store.mark_failure(peer);
      }
   }
   if (attempted > 0 && sent == 0) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "GossipSub publish could not reach any candidate peer");
   }
   co_return value;
}

pubsub::snapshot node::pubsub_snapshot() const {
   return impl_->pubsub_snapshot();
}

boost::asio::awaitable<std::chrono::milliseconds> node::async_ping(peer_id peer) {
   co_return co_await async_ping(std::move(peer), open_options{});
}

boost::asio::awaitable<std::chrono::milliseconds> node::async_ping(peer_id peer, open_options options) {
   auto started = std::chrono::steady_clock::now();
   auto stream = co_await async_open_protocol_stream(std::move(peer), builtins::ping, std::move(options));
   const auto payload = forge::crypto::random_bytes(32);
   co_await stream.async_write(payload);
   const auto reply = co_await stream.async_read();
   if (reply != payload) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "libp2p ping payload mismatch");
   }
   co_await stream.async_close();
   co_return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
}

boost::asio::awaitable<hole_punch::status>
node::async_attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer, std::chrono::milliseconds timeout) {
   auto self = impl_;
   co_return co_await self->attempt_hole_punch(std::move(peer), std::move(relay_peer), timeout);
}

boost::asio::awaitable<forge::p2p::stream> node::async_open_protocol_stream(peer_id peer, protocol_id protocol) {
   return async_open_protocol_stream(std::move(peer), std::move(protocol), open_options{});
}

boost::asio::awaitable<forge::p2p::stream> node::async_open_protocol_stream(peer_id peer, protocol_id protocol,
                                                                          node::open_options options) {
   validate_operation_timeout(options.timeout, "P2P protocol open timeout");
   validate_operation_timeout(options.direct_attempt_timeout, "P2P direct attempt timeout");
   validate_operation_timeout(options.relay_attempt_timeout, "P2P relay attempt timeout");
   if (options.max_direct_endpoints == 0 || options.max_relay_candidates == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P path attempt limits must be positive");
   }
   auto self = impl_;
   auto effective = options;
   effective.allow_relay =
       effective.allow_relay && self->options.path_policy.allow_relay && self->options.relay_policy.client_enabled;
   effective.allow_hole_punch = effective.allow_hole_punch && self->options.path_policy.allow_hole_punch;
   effective.max_direct_endpoints =
       std::min(effective.max_direct_endpoints, self->options.path_policy.max_direct_endpoints);
   effective.max_relay_candidates =
       std::min(effective.max_relay_candidates, self->options.path_policy.max_relay_candidates);
   const auto started = std::chrono::steady_clock::now();
   auto last_kind = std::optional<exceptions::code>{};
   auto last_message = std::string{};
   if (self->options.path_policy.allow_direct) {
      try {
         co_return co_await self->open_protocol_direct(
             peer, protocol, effective.timeout, effective.max_direct_endpoints, effective.direct_attempt_timeout);
      } catch (const forge::exceptions::base& error) {
         last_kind = p2p_code(error);
         last_message = error.what();
         if (p2p_code(error) == exceptions::code::unsupported_protocol || p2p_code(error) == exceptions::code::protocol_error ||
             p2p_code(error) == exceptions::code::codec_error) {
            throw;
         }
         try {
            (void)remaining_timeout(started, effective.timeout, "P2P protocol open");
         } catch (const forge::exceptions::base&) {
            throw;
         }
         if (!effective.allow_relay && !(effective.allow_hole_punch && effective.relay_peer)) {
            throw;
         }
      }
   }

   auto relay_candidates = std::vector<peer_id>{};
   if (effective.relay_peer) {
      relay_candidates.push_back(*effective.relay_peer);
   } else if (effective.allow_relay || effective.allow_hole_punch) {
      relay_candidates =
          self->fresh_outbound_relay_candidates(effective.max_relay_candidates, self->options.relay_policy.refresh_margin);
      if (relay_candidates.empty() && self->options.relay_policy.auto_discovery_enabled) {
         const auto remaining = remaining_timeout(started, effective.timeout, "P2P AutoRelay refresh");
         const auto refresh_timeout = attempt_timeout(remaining, effective.relay_attempt_timeout, "P2P AutoRelay refresh");
         try {
            (void)co_await self->refresh_relay_candidates(peer, refresh_timeout);
            relay_candidates = self->fresh_outbound_relay_candidates(effective.max_relay_candidates,
                                                                     self->options.relay_policy.refresh_margin);
         } catch (const forge::exceptions::base& error) {
            last_kind = p2p_code(error);
            last_message = error.what();
         }
      }
   }

   if (effective.allow_hole_punch) {
      for (const auto& relay_peer : relay_candidates) {
         const auto remaining = remaining_timeout(started, effective.timeout, "P2P hole punch");
         const auto per_attempt = attempt_timeout(remaining, effective.relay_attempt_timeout, "P2P hole punch attempt");
         try {
            const auto status = co_await self->attempt_hole_punch(peer, relay_peer, per_attempt);
            if (status == hole_punch::status::succeeded) {
               co_return co_await self->open_protocol_direct(
                   peer, protocol, remaining_timeout(started, effective.timeout, "P2P protocol open after hole punch"),
                   effective.max_direct_endpoints, effective.direct_attempt_timeout);
            }
         } catch (const forge::exceptions::base& error) {
            last_kind = p2p_code(error);
            last_message = error.what();
         }
      }
   }

   if (!effective.allow_relay) {
      if (last_kind) {
         FORGE_THROW_CODE(*last_kind, last_message);
      }
      FORGE_THROW_EXCEPTION(exceptions::relay_not_available, "P2P relay fallback is disabled");
   }

   if (relay_candidates.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::relay_not_available, "P2P path manager found no reserved relay candidate");
   }
   self->record_direct_failure(peer);
   for (const auto& relay_peer : relay_candidates) {
      const auto remaining = remaining_timeout(started, effective.timeout, "P2P protocol open");
      const auto per_attempt = attempt_timeout(remaining, effective.relay_attempt_timeout, "P2P relay path attempt");
      try {
         co_return co_await self->open_protocol_via_relay(peer, protocol, relay_peer, per_attempt);
      } catch (const forge::exceptions::base& error) {
         last_kind = p2p_code(error);
         last_message = error.what();
      }
   }
   if (last_kind) {
      FORGE_THROW_CODE(*last_kind, last_message);
   }
   FORGE_THROW_EXCEPTION(exceptions::relay_not_available, "P2P path manager exhausted relay candidates");
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
      self->direct_registry.stop();
      for (auto& [_, session] : self->sessions) {
         session->closed = true;
         sessions.push_back(session);
      }
      self->connections.clear(self->resources);
      self->sessions.clear();
      self->inbound_relay_reservations.clear();
      self->outbound_relay_reservations.clear();
      self->pubsub_value.outbound_streams.clear();
      self->pubsub_value.active_validations_by_peer.clear();
      self->pubsub_value.active_validations = 0;
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
      impl_->direct_registry.stop();
      for (auto& [_, session] : impl_->sessions) {
         session->closed = true;
         session->connection.cancel();
      }
      impl_->connections.clear(impl_->resources);
      impl_->sessions.clear();
      impl_->inbound_relay_reservations.clear();
      impl_->outbound_relay_reservations.clear();
      impl_->metrics_value.active_sessions = 0;
      impl_->metrics_value.active_relay_reservations = 0;
      impl_->metrics_value.stopped = true;
   }
}

} // namespace forge::p2p
