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
      exceptions::raise(exceptions::code::invalid_options, "P2P protocol handler requires protocol id and handler");
   }
   auto lock = std::scoped_lock{impl_->mutex};
   if (impl_->handlers.size() >= impl_->options.limits.max_protocol_handlers) {
      exceptions::raise(exceptions::code::backpressure_rejected, "P2P max protocol handlers reached");
   }
   const auto [_, inserted] = impl_->handlers.emplace(std::move(protocol), std::move(handler));
   if (!inserted) {
      exceptions::raise(exceptions::code::duplicate_protocol, "duplicate P2P protocol handler");
   }
}

boost::asio::awaitable<void> node::async_listen(fcl::quic::endpoint endpoint) {
   auto self = impl_;
   {
      auto lock = std::scoped_lock{self->mutex};
      if (self->stopped) {
         exceptions::raise(exceptions::code::closed, "P2P node is stopped");
      }
      if (self->listener) {
         exceptions::raise(exceptions::code::invalid_options, "P2P node is already listening");
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

boost::asio::awaitable<reachability::state> node::async_probe_reachability(peer_id observer) {
   auto self = impl_;
   auto endpoints = std::vector<endpoint>{};
   if (auto endpoint = self->local_endpoint_for_control()) {
      endpoints.push_back(self->p2p_endpoint_for(*endpoint));
   }
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
      auto observed = std::optional<fcl::quic::endpoint>{};
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
            exceptions::raise(exceptions::code::protocol_error, "AutoNAT v2 probe expected dial response");
         }
         if (message.dial_response->status == reachability::v2::response_status::ok &&
             message.dial_response->dial_status == reachability::v2::dial_status::ok) {
            state = reachability::state::publicly_reachable;
            if (message.dial_response->index < endpoints.size()) {
               observed = endpoints[message.dial_response->index].quic_endpoint();
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
      exceptions::raise(exceptions::code::protocol_error, "AutoNAT v2 probe exceeded message exchange limit");
   } catch (const fcl::exception::base& error) {
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
      exceptions::raise(exceptions::code::protocol_error, "AutoNAT probe expected dial response");
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
       response.response->endpoint ? std::make_optional(response.response->endpoint->quic_endpoint()) : std::nullopt);
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
   auto candidates = self->store.closest_routing_peers(target, self->options.limits.dht.alpha);
   for (const auto& candidate : candidates) {
      try {
         auto stream = co_await self->open_protocol_direct(candidate.id, builtins::kad_dht,
                                                           self->options.limits.dht.query_timeout);
         co_await stream.async_write(dht::codec::encode(dht::message{
             .type = dht::message_type::find_node,
             .key_value = target,
         },
                                                        self->options.limits.dht));
         auto buffer = std::vector<std::uint8_t>{};
         auto response = dht::codec::decode(
             co_await async_read_length_delimited(stream, buffer, self->options.limits.dht.max_message_size),
             self->options.limits.dht);
         for (const auto& closer : response.closer_peers) {
            self->store.upsert_routing_peer(closer, discovery::source::dht,
                                            std::chrono::system_clock::now() + self->options.limits.dht.refresh_interval);
            if (closer.id == peer) {
               result.complete = true;
            }
            result.closest_peers.push_back(closer);
         }
         co_await stream.async_close();
         if (result.complete) {
            co_return result;
         }
      } catch (const fcl::exception::base&) {
         self->store.mark_failure(candidate.id);
      }
   }
   co_return result;
}

boost::asio::awaitable<void> node::async_provide(dht::key key) {
   auto self = impl_;
   auto endpoints = std::vector<endpoint>{};
   if (auto local_endpoint = self->local_endpoint_for_control()) {
      endpoints.push_back(self->p2p_endpoint_for(*local_endpoint));
   }
   auto provider = dht::peer{.id = self->local, .endpoints = endpoints, .connection = dht::connection_type::connected};
   self->store.upsert_provider(peer_store::provider_record{
       .key = key,
       .provider = provider,
       .discovered_by = discovery::source::dht,
       .expires_at = std::chrono::system_clock::now() + self->options.limits.dht.provider_record_ttl,
   });
   auto candidates = self->store.closest_routing_peers(key, self->options.limits.dht.replication);
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
      } catch (const fcl::exception::base&) {
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
   auto candidates = self->store.closest_routing_peers(key, self->options.limits.dht.alpha);
   for (const auto& candidate : candidates) {
      try {
         auto stream = co_await self->open_protocol_direct(candidate.id, builtins::kad_dht,
                                                           self->options.limits.dht.query_timeout);
         co_await stream.async_write(dht::codec::encode(dht::message{
             .type = dht::message_type::get_providers,
             .key_value = key,
         },
                                                        self->options.limits.dht));
         auto buffer = std::vector<std::uint8_t>{};
         auto response = dht::codec::decode(
             co_await async_read_length_delimited(stream, buffer, self->options.limits.dht.max_message_size),
             self->options.limits.dht);
         for (const auto& provider : response.provider_peers) {
            self->store.upsert_provider(peer_store::provider_record{
                .key = key,
                .provider = provider,
                .discovered_by = discovery::source::dht,
                .expires_at = std::chrono::system_clock::now() + self->options.limits.dht.provider_record_ttl,
            });
            out.push_back(provider);
         }
         for (const auto& closer : response.closer_peers) {
            self->store.upsert_routing_peer(closer, discovery::source::dht,
                                            std::chrono::system_clock::now() + self->options.limits.dht.refresh_interval);
         }
         co_await stream.async_close();
      } catch (const fcl::exception::base&) {
         self->store.mark_failure(candidate.id);
      }
   }
   co_return out;
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
      exceptions::raise(exceptions::code::protocol_error, "rendezvous expected register response");
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
      exceptions::raise(exceptions::code::protocol_error, "rendezvous expected discover response");
   }
   for (const auto& registration : response.discover_response_value->registrations) {
      if (valid_peer_id(registration.peer)) {
         self->store.upsert_rendezvous(registration);
      }
   }
   co_await stream.async_close();
   co_return *response.discover_response_value;
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
      exceptions::raise(exceptions::code::protocol_error, "libp2p ping payload mismatch");
   }
   co_await stream.async_close();
   co_return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
}

boost::asio::awaitable<hole_punch::status>
node::async_attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer, std::chrono::milliseconds timeout) {
   auto self = impl_;
   co_return co_await self->attempt_hole_punch(std::move(peer), std::move(relay_peer), timeout);
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
      exceptions::raise(exceptions::code::invalid_options, "P2P path attempt limits must be positive");
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
      } catch (const fcl::exception::base& error) {
         last_kind = p2p_code(error);
         last_message = error.what();
         if (p2p_code(error) == exceptions::code::unsupported_protocol || p2p_code(error) == exceptions::code::protocol_error ||
             p2p_code(error) == exceptions::code::codec_error) {
            throw;
         }
         try {
            (void)remaining_timeout(started, effective.timeout, "P2P protocol open");
         } catch (const fcl::exception::base&) {
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
         if (relay_candidates.size() >= effective.max_relay_candidates) {
            break;
         }
         relay_candidates.push_back(record.peer);
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
         } catch (const fcl::exception::base& error) {
            last_kind = p2p_code(error);
            last_message = error.what();
         }
      }
   }

   if (!effective.allow_relay) {
      if (last_kind) {
         exceptions::raise(*last_kind, last_message);
      }
      exceptions::raise(exceptions::code::relay_not_available, "P2P relay fallback is disabled");
   }

   if (relay_candidates.empty()) {
      exceptions::raise(exceptions::code::relay_not_available, "P2P path manager found no reserved relay candidate");
   }
   self->record_direct_failure(peer);
   for (const auto& relay_peer : relay_candidates) {
      const auto remaining = remaining_timeout(started, effective.timeout, "P2P protocol open");
      const auto per_attempt = attempt_timeout(remaining, effective.relay_attempt_timeout, "P2P relay path attempt");
      try {
         co_return co_await self->open_protocol_via_relay(peer, protocol, relay_peer, per_attempt);
      } catch (const fcl::exception::base& error) {
         last_kind = p2p_code(error);
         last_message = error.what();
      }
   }
   if (last_kind) {
      exceptions::raise(*last_kind, last_message);
   }
   exceptions::raise(exceptions::code::relay_not_available, "P2P path manager exhausted relay candidates");
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
