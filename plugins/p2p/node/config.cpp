module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.p2p.node.plugin;

import forge.transport.api.options;
import forge.app.views;
import forge.asio.runtime;
import forge.config.component;
import forge.config.decode;
import forge.exceptions;
import forge.p2p.endpoint;
import forge.p2p.identity;
import forge.p2p.node;
import forge.p2p.protocol;
import forge.p2p.pubsub;
import forge.p2p.scoring;
import forge.plugins.p2p.node.exceptions;
import forge.plugins.p2p.node.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"

namespace forge::plugins::p2p::node {
namespace {

[[nodiscard]] forge::p2p::path::policy parse_path_policy(path_policy value, bool relay_client_enabled,
                                                       std::size_t relay_max_candidates) {
   if (relay_max_candidates == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "P2P relay candidate limit must be positive");
   }
   switch (value) {
   case path_policy::direct_only:
      return forge::p2p::path::policy{
         .allow_direct = true,
         .allow_hole_punch = false,
         .allow_relay = false,
         .max_relay_candidates = relay_max_candidates,
      };
   case path_policy::direct_preferred:
      return forge::p2p::path::policy{
         .allow_direct = true,
         .allow_hole_punch = relay_client_enabled,
         .allow_relay = relay_client_enabled,
         .max_relay_candidates = relay_max_candidates,
      };
   case path_policy::relay_only:
      return forge::p2p::path::policy{
         .allow_direct = false,
         .allow_hole_punch = false,
         .allow_relay = relay_client_enabled,
         .max_relay_candidates = relay_max_candidates,
      };
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_config, "invalid P2P path policy");
}

} // namespace

std::chrono::milliseconds to_ms(std::uint64_t value) {
   return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value)};
}

config decode_config(const forge::config::component_view& view) {
   auto decoded = forge::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                          forge::config::format_decode_diagnostics("invalid P2P node config",
                                                                 decoded.diagnostics));
   }
   return std::move(decoded.value);
}

parsed_policy parse_policy(const config& config) {
   const auto relay_max_candidates = static_cast<std::size_t>(config.relay_max_candidates);
   return parsed_policy{
      .path = parse_path_policy(config.path_policy, config.relay_client_enabled, relay_max_candidates),
      .relay_client_enabled = config.relay_client_enabled,
      .relay_server_enabled = config.relay_server_enabled,
      .relay_public_allowed =
         config.relay_public_allowed || config.relay_trust == relay_trust_policy::public_allowed,
      .relay_reservation_ttl = to_ms(config.relay_reservation_ttl_ms),
      .relay_max_candidates = relay_max_candidates,
   };
}

std::vector<forge::p2p::endpoint> parse_endpoint_list(const std::vector<std::string>& values) {
   auto out = std::vector<forge::p2p::endpoint>{};
   out.reserve(values.size());
   for (const auto& value : values) {
      try {
         out.push_back(forge::p2p::parse_endpoint(value));
      } catch (const std::exception& error) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "invalid P2P endpoint",
                             forge::exceptions::ctx("endpoint", value), forge::exceptions::ctx("error", error.what()));
      }
   }
   return out;
}

void apply_config(plugin::impl& state, const config& config) {
   state.policy = parse_policy(config);
   state.api_options = forge::transport::api::options{
      .codec = forge::api::codec_id{.value = config.api_codec},
      .max_inflight = static_cast<std::size_t>(config.max_inflight_per_peer),
      .deadline = to_ms(config.api_deadline_ms),
      .max_frame_size = static_cast<std::uint32_t>(config.api_max_frame_size),
   };
   state.listen = parse_endpoint_list(config.listen);
   state.bootstrap = parse_endpoint_list(config.bootstrap);
   state.options.advertised_endpoints = parse_endpoint_list(config.advertised_endpoints);
   state.options.certificate_pem = config.certificate_pem;
   state.options.private_key_pem = config.private_key_pem;
   state.options.capabilities = forge::p2p::capability_set{
      .bits = forge::p2p::capabilities::direct_quic | forge::p2p::capabilities::peer_exchange,
   };
   if (state.policy.relay_client_enabled) {
      state.options.capabilities.add(forge::p2p::capabilities::hole_punching);
   }
   if (state.policy.relay_server_enabled) {
      state.options.capabilities.add(forge::p2p::capabilities::relay);
      state.options.capabilities.add(forge::p2p::capabilities::relay_reservation);
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
   state.options.explicit_peer_id =
      peer_id.empty() ? std::nullopt : std::make_optional(forge::p2p::peer_id{.value = peer_id});
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

} // namespace forge::plugins::p2p::node
