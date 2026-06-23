module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

export module forge.p2p.scoring;

import forge.p2p.identity;

export namespace forge::p2p {

struct path {
   enum class kind { direct, hole_punch, relay };

   struct candidate {
      kind kind = kind::direct;
      std::optional<peer_id> relay_peer;
      double score = 0.0;
   };

   struct policy {
      bool allow_direct = true;
      bool allow_hole_punch = true;
      bool allow_relay = true;
      std::size_t max_direct_endpoints = 4;
      std::size_t max_relay_candidates = 4;
   };

   struct result {
      kind kind = kind::direct;
      bool succeeded = false;
      std::optional<peer_id> relay_peer;
      std::chrono::milliseconds latency{0};
   };

   struct failure {
      kind kind = kind::direct;
      std::string reason;
      std::chrono::system_clock::time_point backoff_until{};
   };

   struct observation {
      kind kind = kind::direct;
      std::chrono::milliseconds latency{0};
      std::uint64_t failures = 0;
      std::uint64_t successes = 0;
      std::uint64_t in_flight = 0;
      bool last_success = false;
   };
};

[[nodiscard]] double score_path(const path::observation& observation) noexcept;

} // namespace forge::p2p
