module;

#include <chrono>
#include <cstdint>

export module fcl.p2p.scoring;

export namespace fcl::p2p {

struct path {
   enum class kind { direct, relay };

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

} // namespace fcl::p2p
