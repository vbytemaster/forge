module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_node.plugin;

import fcl.api.transport.options;
import fcl.asio.runtime;
import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.p2p.endpoint;
import fcl.p2p.identity;
import fcl.p2p.node;
import fcl.p2p.protocol;
import fcl.p2p.pubsub;
import fcl.p2p.scoring;
import fcl.plugins.p2p_node.exceptions;
import fcl.plugins.p2p_node.types;

#include "details/state.hxx"
#include "details/config.hxx"

namespace fcl::plugins::p2p_node {
namespace {

void validate_relay_trust(const std::string& value) {
   if (value == "known-only" || value == "public-allowed") {
      return;
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_config, "invalid P2P relay trust policy",
                       fcl::exceptions::ctx("trust", value));
}

[[nodiscard]] fcl::p2p::path::policy parse_path_policy(const std::string& value, bool relay_client_enabled,
                                                       std::size_t relay_max_candidates) {
   if (relay_max_candidates == 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "P2P relay candidate limit must be positive");
   }
   if (value == "direct-only") {
      return fcl::p2p::path::policy{
         .allow_direct = true,
         .allow_hole_punch = false,
         .allow_relay = false,
         .max_relay_candidates = relay_max_candidates,
      };
   }
   if (value == "direct-preferred") {
      return fcl::p2p::path::policy{
         .allow_direct = true,
         .allow_hole_punch = relay_client_enabled,
         .allow_relay = relay_client_enabled,
         .max_relay_candidates = relay_max_candidates,
      };
   }
   if (value == "relay-only") {
      return fcl::p2p::path::policy{
         .allow_direct = false,
         .allow_hole_punch = false,
         .allow_relay = relay_client_enabled,
         .max_relay_candidates = relay_max_candidates,
      };
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_config, "invalid P2P path policy",
                       fcl::exceptions::ctx("policy", value));
}

} // namespace

fcl::p2p::peer_id default_test_peer() {
   return fcl::p2p::make_peer_id(
      {.type = fcl::p2p::public_key::type::ed25519, .data = std::vector<std::uint8_t>(32, 1)});
}

std::chrono::milliseconds to_ms(std::uint64_t value) {
   return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value)};
}

config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      auto message = std::string{"invalid P2P node config"};
      if (!decoded.diagnostics.entries.empty()) {
         const auto& first = decoded.diagnostics.entries.front();
         message += ": " + first.path + " " + first.code + " " + first.message;
      }
      FCL_THROW_EXCEPTION(exceptions::invalid_config, message);
   }
   return std::move(decoded.value);
}

parsed_policy parse_policy(const config& config) {
   validate_relay_trust(config.relay_trust);
   const auto relay_max_candidates = static_cast<std::size_t>(config.relay_max_candidates);
   return parsed_policy{
      .path = parse_path_policy(config.path_policy, config.relay_client_enabled, relay_max_candidates),
      .relay_client_enabled = config.relay_client_enabled,
      .relay_server_enabled = config.relay_server_enabled,
      .relay_public_allowed = config.relay_public_allowed || config.relay_trust == "public-allowed",
      .relay_reservation_ttl = to_ms(config.relay_reservation_ttl_ms),
      .relay_max_candidates = relay_max_candidates,
   };
}

std::vector<fcl::p2p::endpoint> parse_endpoint_list(const std::vector<std::string>& values) {
   auto out = std::vector<fcl::p2p::endpoint>{};
   out.reserve(values.size());
   for (const auto& value : values) {
      try {
         out.push_back(fcl::p2p::parse_endpoint(value));
      } catch (const std::exception& error) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "invalid P2P endpoint",
                             fcl::exceptions::ctx("endpoint", value), fcl::exceptions::ctx("error", error.what()));
      }
   }
   return out;
}

void apply_config(plugin::impl& state, const config& config) {
   state.policy = parse_policy(config);
   state.api_options = fcl::api::transport::options{
      .codec = fcl::api::codec_id{.value = config.api_codec},
      .max_inflight = static_cast<std::size_t>(config.max_inflight_per_peer),
      .deadline = to_ms(config.api_deadline_ms),
      .max_frame_size = static_cast<std::uint32_t>(config.api_max_frame_size),
   };
   state.listen = parse_endpoint_list(config.listen);
   state.bootstrap = parse_endpoint_list(config.bootstrap);
   state.options.advertised_endpoints = parse_endpoint_list(config.advertised_endpoints);
   state.options.certificate_pem = config.certificate_pem;
   state.options.private_key_pem = config.private_key_pem;
   state.options.capabilities = fcl::p2p::capability_set{
      .bits = fcl::p2p::capabilities::direct_quic | fcl::p2p::capabilities::peer_exchange,
   };
   if (state.policy.relay_client_enabled) {
      state.options.capabilities.add(fcl::p2p::capabilities::hole_punching);
   }
   if (state.policy.relay_server_enabled) {
      state.options.capabilities.add(fcl::p2p::capabilities::relay);
      state.options.capabilities.add(fcl::p2p::capabilities::relay_reservation);
   }
   state.options.path_policy = state.policy.path;
   state.options.relay_policy.client_enabled = state.policy.relay_client_enabled;
   state.options.relay_policy.service_enabled = state.policy.relay_server_enabled;
   state.options.relay_policy.public_relay_allowed = state.policy.relay_public_allowed;
   state.options.relay_policy.max_candidates_per_refresh = state.policy.relay_max_candidates;
   state.options.relay_policy.target_reservations =
      std::min(state.options.relay_policy.target_reservations, state.policy.relay_max_candidates);
   if (state.options.relay_policy.target_reservations == 0) {
      state.options.relay_policy.target_reservations = 1;
   }
   state.options.limits.relay.reservation_ttl = state.policy.relay_reservation_ttl;
   const auto& peer_id = config.peer_id;
   state.options.explicit_peer_id = peer_id.empty() ? default_test_peer() : fcl::p2p::peer_id{.value = peer_id};
   state.options.limits.max_sessions = static_cast<std::size_t>(config.max_sessions);
   state.options.limits.session_low_watermark =
      std::min(state.options.limits.session_low_watermark, state.options.limits.max_sessions);
   state.options.limits.max_inbound_sessions =
      std::min(state.options.limits.max_inbound_sessions, state.options.limits.max_sessions);
   state.options.limits.max_outbound_sessions =
      std::min(state.options.limits.max_outbound_sessions, state.options.limits.max_sessions);
   state.options.limits.max_protocol_handlers = static_cast<std::size_t>(config.max_protocol_handlers);
   state.options.allow_insecure_test_mode = config.allow_insecure_test_mode;
}

} // namespace fcl::plugins::p2p_node
