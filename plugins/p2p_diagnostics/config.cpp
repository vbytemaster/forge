module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_diagnostics.plugin;

import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.p2p.diagnostics;
import fcl.p2p.identity;
import fcl.p2p.pubsub;
import fcl.p2p.resource_manager;
import fcl.plugins.p2p_diagnostics.api;
import fcl.plugins.p2p_diagnostics.exceptions;
import fcl.plugins.p2p_diagnostics.types;

#include "details/config.hxx"

namespace fcl::plugins::p2p_diagnostics {

config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config,
                          fcl::config::format_decode_diagnostics("invalid P2P diagnostics config",
                                                                 decoded.diagnostics));
   }
   return std::move(decoded.value);
}

void validate_config(const config& value) {
   static_cast<void>(value);
}

fcl::p2p::diagnostics::options configured_options(const config& value) {
   return fcl::p2p::diagnostics::options{
      .max_peers = static_cast<std::size_t>(value.max_peers),
      .max_sessions = static_cast<std::size_t>(value.max_sessions),
      .max_endpoints_per_peer = static_cast<std::size_t>(value.max_endpoints_per_peer),
      .max_protocols_per_peer = static_cast<std::size_t>(value.max_protocols_per_peer),
      .max_relay_reservations_per_peer = static_cast<std::size_t>(value.max_relay_reservations_per_peer),
   };
}

std::vector<fcl::p2p::diagnostics::peer>
filter_peers(const fcl::p2p::diagnostics::snapshot& snapshot, const filter& filter) {
   auto connected = std::set<fcl::p2p::peer_id>{};
   if (filter.only_connected) {
      for (const auto& session : snapshot.sessions) {
         connected.insert(session.remote_peer);
      }
   }

   auto out = std::vector<fcl::p2p::diagnostics::peer>{};
   out.reserve(snapshot.peers.size());
   for (const auto& peer : snapshot.peers) {
      if (filter.peer.has_value() && peer.peer != *filter.peer) {
         continue;
      }
      if (filter.only_connected && !connected.contains(peer.peer)) {
         continue;
      }
      if (filter.only_protected && !peer.protected_peer) {
         continue;
      }
      out.push_back(peer);
      if (filter.limit != 0 && out.size() >= filter.limit) {
         break;
      }
   }
   return out;
}

} // namespace fcl::plugins::p2p_diagnostics
