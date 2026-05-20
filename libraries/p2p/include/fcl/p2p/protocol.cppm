module;

#include <compare>
#include <cstdint>
#include <string>

export module fcl.p2p.protocol;

export namespace fcl::p2p {

struct protocol_id {
   std::string value;

   [[nodiscard]] friend bool operator==(const protocol_id&, const protocol_id&) noexcept = default;
   [[nodiscard]] friend auto operator<=>(const protocol_id&, const protocol_id&) noexcept = default;
};

struct capability_set {
   std::uint64_t bits = 0;

   [[nodiscard]] constexpr bool has(std::uint64_t flag) const noexcept {
      return (bits & flag) == flag;
   }

   constexpr void add(std::uint64_t flag) noexcept {
      bits |= flag;
   }
};

namespace capabilities {
inline constexpr std::uint64_t direct_quic = 1ULL << 0;
inline constexpr std::uint64_t relay = 1ULL << 1;
inline constexpr std::uint64_t peer_exchange = 1ULL << 2;
inline constexpr std::uint64_t dht = 1ULL << 3;
inline constexpr std::uint64_t autonat = 1ULL << 4;
inline constexpr std::uint64_t hole_punching = 1ULL << 5;
inline constexpr std::uint64_t relay_reservation = 1ULL << 6;
} // namespace capabilities

enum class reachability_state : std::uint16_t {
   unknown = 0,
   publicly_reachable = 1,
   private_network = 2,
   blocked = 3,
   relay_only = 4
};

enum class hole_punch_status : std::uint16_t { not_attempted = 0, prepared = 1, synced = 2, succeeded = 3, failed = 4 };

namespace builtins {
inline const protocol_id control{.value = "/fcl/p2p/control/1"};
inline const protocol_id relay{.value = "/fcl/p2p/relay/1"};
inline const protocol_id echo{.value = "/fcl/p2p/echo/1"};
inline const protocol_id ping{.value = "/ipfs/ping/1.0.0"};
inline const protocol_id identify{.value = "/ipfs/id/1.0.0"};
inline const protocol_id identify_push{.value = "/ipfs/id/push/1.0.0"};
} // namespace builtins

} // namespace fcl::p2p
