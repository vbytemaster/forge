module;

#include <algorithm>
#include <chrono>
#include <span>
#include <vector>

module forge.p2p.node;

import forge.p2p.discovery;
import forge.p2p.endpoint;
import forge.p2p.identity;
import forge.p2p.peer_store;
import forge.p2p.protocol;
import forge.p2p.relay;

#include "details/relay_discovery.hxx"

namespace forge::p2p::relay_discovery {
namespace {

[[nodiscard]] bool usable_endpoint(const peer_store::endpoint_record& value) {
   return value.kind == path::kind::direct && !value.relay_peer &&
          (value.endpoint.is_direct_quic() || value.endpoint.is_direct_tcp());
}

[[nodiscard]] bool relay_capable(const peer_store::record& value) {
   return value.capabilities.has(capabilities::relay) && value.capabilities.has(capabilities::relay_reservation);
}

[[nodiscard]] bool fresh_discovery(const peer_store::record& value, std::chrono::system_clock::time_point now) {
   return value.discovery_expires_at == std::chrono::system_clock::time_point{} || value.discovery_expires_at > now;
}

[[nodiscard]] bool outside_backoff(const peer_store::record& value, std::chrono::system_clock::time_point now) {
   return value.discovery_backoff_until == std::chrono::system_clock::time_point{} ||
          value.discovery_backoff_until <= now;
}

[[nodiscard]] bool has_usable_endpoint(const peer_store::record& value) {
   return std::ranges::any_of(value.endpoints, usable_endpoint);
}

} // namespace

std::vector<candidate> select_candidates(std::span<const peer_store::record> records, const request& value) {
   auto out = std::vector<candidate>{};
   if (value.limit == 0) {
      return out;
   }
   out.reserve(std::min(value.limit, records.size()));
   for (const auto& record : records) {
      if (record.peer == value.local || record.peer == value.target) {
         continue;
      }
      if (!relay_capable(record) || !fresh_discovery(record, value.now) || !outside_backoff(record, value.now) ||
          !has_usable_endpoint(record)) {
         continue;
      }
      out.push_back(candidate{.peer = record.peer, .source = record.discovered_by, .score = record.score});
   }
   std::stable_sort(out.begin(), out.end(), [](const auto& left, const auto& right) {
      if (left.score != right.score) {
         return left.score > right.score;
      }
      return left.peer.to_string() < right.peer.to_string();
   });
   if (out.size() > value.limit) {
      out.resize(value.limit);
   }
   return out;
}

void backoff_candidate(peer_store& store, const peer_id& peer, std::chrono::system_clock::time_point until) {
   auto record = store.find(peer).value_or(peer_store::record{.peer = peer});
   record.discovery_backoff_until = until;
   ++record.failures;
   store.upsert(std::move(record));
}

void prune_expired_reservations(peer_store& store, std::chrono::system_clock::time_point now) {
   for (auto record : store.snapshot()) {
      const auto before = record.relay_reservations.size();
      std::erase_if(record.relay_reservations, [&](const peer_store::relay_record& value) {
         return value.expires_at != std::chrono::system_clock::time_point{} && value.expires_at <= now;
      });
      if (record.relay_reservations.size() != before) {
         store.upsert(std::move(record));
      }
   }
}

} // namespace forge::p2p::relay_discovery
