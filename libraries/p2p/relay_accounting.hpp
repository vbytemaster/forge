#pragma once

#include <cstdint>

namespace forge::p2p::detail {

template <typename ResourceManager, typename Metrics, typename Reservation>
bool add_relay_bytes(ResourceManager& resources, Metrics& metrics, Reservation* reservation,
                     bool require_reservation, std::uint64_t bytes) {
   if (reservation == nullptr) {
      if (require_reservation) {
         ++metrics.relay_rejections;
         return false;
      }
   } else if (bytes > reservation->max_bytes || reservation->bytes > reservation->max_bytes - bytes) {
      ++metrics.relay_rejections;
      return false;
   }

   if (!resources.add_relay_bytes(bytes)) {
      ++metrics.relay_rejections;
      return false;
   }
   metrics.relay_bytes += bytes;
   if (reservation != nullptr) {
      reservation->bytes += bytes;
   }
   return true;
}

} // namespace forge::p2p::detail
