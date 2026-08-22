#include "details/rendezvous_time.hxx"

#include <limits>
#include <ratio>

namespace forge::net::p2p::detail {

std::optional<std::chrono::seconds> rendezvous_ttl_from_wire(std::uint64_t value) noexcept {
   if (value > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
      return std::nullopt;
   }
   return std::chrono::seconds{static_cast<std::int64_t>(value)};
}

std::chrono::system_clock::time_point
rendezvous_expiry_after(std::chrono::system_clock::time_point now, std::chrono::seconds ttl) noexcept {
   if (ttl <= std::chrono::seconds::zero() || now == std::chrono::system_clock::time_point::max()) {
      return now;
   }

   using system_duration = std::chrono::system_clock::duration;
   using conversion = std::ratio_divide<std::chrono::seconds::period, system_duration::period>;
   const auto increment =
       (static_cast<unsigned __int128>(ttl.count()) * static_cast<unsigned __int128>(conversion::num)) /
       static_cast<unsigned __int128>(conversion::den);
   const auto maximum = static_cast<__int128>((std::numeric_limits<system_duration::rep>::max)());
   const auto base = static_cast<__int128>(now.time_since_epoch().count());
   const auto available = maximum - base;
   if (available <= 0 || increment >= static_cast<unsigned __int128>(available)) {
      return std::chrono::system_clock::time_point::max();
   }
   return std::chrono::system_clock::time_point{
       system_duration{static_cast<system_duration::rep>(base + static_cast<__int128>(increment))}};
}

} // namespace forge::net::p2p::detail
