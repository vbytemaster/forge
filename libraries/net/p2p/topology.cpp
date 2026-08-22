module;

#include <forge/exceptions/macros.hpp>

#include <cmath>
#include <set>
#include <string>
#include <utility>

module forge.net.p2p.topology;

import forge.net.p2p.exceptions;
import forge.net.p2p.endpoint;
import forge.net.p2p.identity;

namespace forge::net::p2p {
namespace {

[[nodiscard]] bool is_direct_endpoint(const endpoint& value) noexcept {
   return value.is_direct_quic() || value.is_direct_tcp();
}

} // namespace

void validate(const topology::policy& policy) {
   if (policy.peers.low == 0 || policy.peers.low > policy.peers.target || policy.peers.target > policy.peers.high) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P topology watermarks must be positive and ordered");
   }
   if (policy.refresh_interval.count() <= 0 || policy.query_timeout.count() <= 0 || policy.max_candidates == 0 ||
       policy.max_parallel_queries == 0 || policy.max_parallel_dials == 0 || policy.max_rendezvous_points == 0 ||
       policy.max_rendezvous_namespaces == 0 || policy.max_peer_exchange_peers == 0 || policy.max_tagged_peers == 0 ||
       policy.max_tags_per_peer == 0 || policy.max_tag_size == 0 || !std::isfinite(policy.retry_jitter) ||
       policy.retry_jitter < 0.0 || policy.retry_jitter >= 1.0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid P2P topology policy limits");
   }
   if (policy.rendezvous_points.size() > policy.max_rendezvous_points) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P topology rendezvous point limit exceeded");
   }
   auto configured_endpoints = std::set<std::string>{};
   auto configured_pairs = std::set<std::pair<std::string, std::string>>{};
   for (const auto& point : policy.rendezvous_points) {
      if (!point.endpoint.peer || !valid_peer_id(*point.endpoint.peer) || !is_direct_endpoint(point.endpoint) ||
          point.endpoint.transport.host.empty() || point.endpoint.transport.port == 0 || point.namespaces.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid configured P2P rendezvous point");
      }
      if (!configured_endpoints.insert(point.endpoint.to_string()).second) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "duplicate configured P2P rendezvous point");
      }

      auto namespaces = std::set<std::string>{};
      for (const auto& namespace_name : point.namespaces) {
         if (namespace_name.empty() || !namespaces.insert(namespace_name).second) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P rendezvous namespaces must be unique and non-empty");
         }
         const auto configured_pair = std::pair{point.endpoint.peer->to_string(), namespace_name};
         if (configured_pairs.contains(configured_pair)) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                                  "duplicate configured P2P rendezvous peer and namespace");
         }
         if (configured_pairs.size() >= policy.max_rendezvous_namespaces) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                                  "P2P topology rendezvous namespace pair limit exceeded");
         }
         configured_pairs.insert(std::move(configured_pair));
      }
   }
}

} // namespace forge::net::p2p
