#pragma once

#include <chrono>
#include <cstddef>
#include <span>
#include <vector>

namespace forge::p2p::relay_discovery {

struct candidate {
   peer_id peer;
   discovery::source source = discovery::source::explicit_config;
   double score = 0.0;
};

struct request {
   peer_id local;
   peer_id target;
   std::chrono::system_clock::time_point now{};
   std::size_t limit = 0;
};

[[nodiscard]] std::vector<candidate> select_candidates(std::span<const peer_store::record> records,
                                                       const request& value);

void backoff_candidate(peer_store& store, const peer_id& peer, std::chrono::system_clock::time_point until);

void prune_expired_reservations(peer_store& store, std::chrono::system_clock::time_point now);

} // namespace forge::p2p::relay_discovery
