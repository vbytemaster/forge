module;

#include <chrono>
#include <cstddef>
#include <cstdint>

export module fcl.p2p.relay;

import fcl.p2p.identity;

export namespace fcl::p2p {

struct relay {
   struct limits {
      std::size_t max_active_relays = 128;
      std::size_t max_reservations = 1024;
      std::size_t max_streams_per_reservation = 64;
      std::uint64_t max_relay_bytes = 256 * 1024 * 1024;
      std::size_t max_queued_bytes = 16 * 1024 * 1024;
      std::chrono::milliseconds max_duration{60'000};
      std::chrono::milliseconds reservation_ttl{60'000};
      bool require_reservation = true;
   };
};

struct relay_reservation {
   struct options {
      std::chrono::milliseconds ttl{60'000};
      std::size_t max_streams = 64;
      std::uint64_t max_bytes = 256 * 1024 * 1024;
      std::size_t max_queued_bytes = 16 * 1024 * 1024;
   };

   struct info {
      peer_id relay_peer;
      std::uint64_t id = 0;
      std::chrono::milliseconds ttl{0};
      std::size_t max_streams = 0;
      std::uint64_t max_bytes = 0;
      std::size_t max_queued_bytes = 0;
   };
};

} // namespace fcl::p2p
