module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_node.plugin;

import fcl.api.transport.options;
import fcl.asio.runtime;
import fcl.exceptions;
import fcl.p2p.endpoint;
import fcl.p2p.identity;
import fcl.p2p.node;
import fcl.p2p.protocol;
import fcl.p2p.pubsub;
import fcl.p2p.scoring;
import fcl.plugins.p2p_node.exceptions;
import fcl.plugins.p2p_node.types;

#include "details/plugin_impl.hxx"
#include "details/config.hxx"

namespace fcl::plugins::p2p_node {
namespace {

[[nodiscard]] bool contains_protocol(
   const std::vector<std::pair<fcl::p2p::protocol_id, fcl::p2p::node::protocol_handler>>& routes,
   const fcl::p2p::protocol_id& protocol) {
   return std::any_of(routes.begin(), routes.end(), [&](const auto& route) {
      return route.first == protocol;
   });
}

} // namespace

fcl::p2p::node& plugin::impl::ensure_node() {
   if (!runtime) {
      FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
   }
   if (!node) {
      if (pubsub_requested) {
         options.capabilities.add(fcl::p2p::capabilities::pubsub);
         options.limits.pubsub = pubsub_options;
      }
      node = std::make_unique<fcl::p2p::node>(*runtime, options);
      raw = node.get();
   }
   return *node;
}

fcl::p2p::node& plugin::impl::require_node() {
   if (!node) {
      FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
   }
   return *node;
}

const fcl::p2p::node& plugin::impl::require_node() const {
   if (!node) {
      FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
   }
   return *node;
}

void plugin::impl::add_route(fcl::p2p::protocol_id protocol, fcl::p2p::node::protocol_handler handler) {
   if (started) {
      FCL_THROW_EXCEPTION(exceptions::route_conflict, "P2P routes must be published before startup",
                          fcl::exceptions::ctx("protocol", protocol.value));
   }
   if (protocol.value.empty() || !handler) {
      FCL_THROW_EXCEPTION(exceptions::route_conflict, "P2P route is invalid");
   }
   if (contains_protocol(routes, protocol)) {
      FCL_THROW_EXCEPTION(exceptions::route_conflict, "duplicate P2P route",
                          fcl::exceptions::ctx("protocol", protocol.value));
   }
   routes.emplace_back(std::move(protocol), std::move(handler));
}

fcl::p2p::node::open_options plugin::impl::open_options_for(remote_options value) const {
   if (value.open_deadline.count() <= 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "P2P remote open deadline must be positive");
   }
   return fcl::p2p::node::open_options{
      .allow_relay = policy.relay_client_enabled && policy.path.allow_relay,
      .timeout = value.open_deadline,
      .direct_attempt_timeout = std::min(std::chrono::milliseconds{2'000}, value.open_deadline),
      .relay_attempt_timeout = std::min(std::chrono::milliseconds{5'000}, value.open_deadline),
      .max_direct_endpoints = policy.path.max_direct_endpoints,
      .max_relay_candidates = policy.path.max_relay_candidates,
      .allow_hole_punch = policy.relay_client_enabled && policy.path.allow_hole_punch,
   };
}

fcl::api::transport::options plugin::impl::api_options_for(const remote_options& value) const {
   auto out = api_options;
   if (value.codec.has_value()) {
      if (value.codec->value.empty()) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "P2P remote API codec override is invalid");
      }
      out.codec = *value.codec;
   }
   if (value.max_inflight.has_value()) {
      if (*value.max_inflight == 0) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "P2P remote API max inflight override is invalid");
      }
      out.max_inflight = *value.max_inflight;
   }
   if (value.deadline.has_value()) {
      out.deadline = *value.deadline;
   }
   if (value.max_frame_size.has_value()) {
      if (*value.max_frame_size == 0) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config,
                             "P2P remote API max frame size override is invalid");
      }
      out.max_frame_size = *value.max_frame_size;
   }
   return out;
}

} // namespace fcl::plugins::p2p_node
