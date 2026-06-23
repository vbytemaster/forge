module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.p2p.node.plugin;

import forge.transport.api.options;
import forge.asio.runtime;
import forge.exceptions;
import forge.p2p.endpoint;
import forge.p2p.identity;
import forge.p2p.node;
import forge.p2p.protocol;
import forge.p2p.pubsub;
import forge.p2p.scoring;
import forge.plugins.p2p.node.exceptions;
import forge.plugins.p2p.node.types;

#include "details/plugin_impl.hxx"
#include "details/config.hxx"

namespace forge::plugins::p2p::node {
namespace {

[[nodiscard]] bool contains_protocol(
   const std::vector<std::pair<forge::p2p::protocol_id, forge::p2p::node::protocol_handler>>& routes,
   const forge::p2p::protocol_id& protocol) {
   return std::any_of(routes.begin(), routes.end(), [&](const auto& route) {
      return route.first == protocol;
   });
}

} // namespace

forge::p2p::node& plugin::impl::ensure_node() {
   if (!runtime) {
      FORGE_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
   }
   if (!node) {
      if (pubsub_requested) {
         options.capabilities.add(forge::p2p::capabilities::pubsub);
         options.limits.pubsub = pubsub_options;
      }
      node = std::make_unique<forge::p2p::node>(*runtime, options);
      raw = node.get();
   }
   return *node;
}

forge::p2p::node& plugin::impl::require_node() {
   if (!node) {
      FORGE_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
   }
   return *node;
}

const forge::p2p::node& plugin::impl::require_node() const {
   if (!node) {
      FORGE_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
   }
   return *node;
}

void plugin::impl::add_route(forge::p2p::protocol_id protocol, forge::p2p::node::protocol_handler handler) {
   if (started) {
      FORGE_THROW_EXCEPTION(exceptions::route_conflict, "P2P routes must be published before startup",
                          forge::exceptions::ctx("protocol", protocol.value));
   }
   if (protocol.value.empty() || !handler) {
      FORGE_THROW_EXCEPTION(exceptions::route_conflict, "P2P route is invalid");
   }
   if (contains_protocol(routes, protocol)) {
      FORGE_THROW_EXCEPTION(exceptions::route_conflict, "duplicate P2P route",
                          forge::exceptions::ctx("protocol", protocol.value));
   }
   routes.emplace_back(std::move(protocol), std::move(handler));
}

forge::p2p::node::open_options plugin::impl::open_options_for(remote_options value) const {
   if (value.open_deadline.count() <= 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "P2P remote open deadline must be positive");
   }
   return forge::p2p::node::open_options{
      .allow_relay = policy.relay_client_enabled && policy.path.allow_relay,
      .timeout = value.open_deadline,
      .direct_attempt_timeout = std::min(std::chrono::milliseconds{2'000}, value.open_deadline),
      .relay_attempt_timeout = std::min(std::chrono::milliseconds{5'000}, value.open_deadline),
      .max_direct_endpoints = policy.path.max_direct_endpoints,
      .max_relay_candidates = policy.path.max_relay_candidates,
      .allow_hole_punch = policy.relay_client_enabled && policy.path.allow_hole_punch,
   };
}

forge::transport::api::options plugin::impl::api_options_for(const remote_options& value) const {
   auto out = api_options;
   if (value.codec.has_value()) {
      if (value.codec->value.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "P2P remote API codec override is invalid");
      }
      out.codec = *value.codec;
   }
   if (value.max_inflight.has_value()) {
      if (*value.max_inflight == 0) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "P2P remote API max inflight override is invalid");
      }
      out.max_inflight = *value.max_inflight;
   }
   if (value.deadline.has_value()) {
      out.deadline = *value.deadline;
   }
   if (value.max_frame_size.has_value()) {
      if (*value.max_frame_size == 0) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                             "P2P remote API max frame size override is invalid");
      }
      out.max_frame_size = *value.max_frame_size;
   }
   return out;
}

} // namespace forge::plugins::p2p::node
