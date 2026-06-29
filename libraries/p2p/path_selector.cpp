module;

#include <algorithm>
#include <chrono>
#include <vector>

module forge.p2p.node;

import forge.p2p.endpoint;
import forge.p2p.peer_store;
import forge.p2p.scoring;

#include "details/path_selector.hxx"

namespace forge::p2p::path_selector {
namespace {

[[nodiscard]] bool supported_direct(const peer_store::endpoint_record& value) {
   return value.kind == path::kind::direct && !value.relay_peer &&
          (value.endpoint.is_direct_quic() || value.endpoint.is_direct_tcp());
}

} // namespace

std::vector<peer_store::endpoint_record> rank_direct(const peer_store::record& record,
                                                     std::chrono::system_clock::time_point now) {
   auto fresh = std::vector<peer_store::endpoint_record>{};
   auto backed_off = std::vector<peer_store::endpoint_record>{};
   for (const auto& endpoint : record.endpoints) {
      if (!supported_direct(endpoint)) {
         continue;
      }
      if (endpoint.backoff_until != std::chrono::system_clock::time_point{} && endpoint.backoff_until > now) {
         backed_off.push_back(endpoint);
      } else {
         fresh.push_back(endpoint);
      }
   }

   auto& selected = fresh.empty() ? backed_off : fresh;
   std::stable_sort(selected.begin(), selected.end(),
                    [](const auto& left, const auto& right) { return left.score > right.score; });
   return selected;
}

} // namespace forge::p2p::path_selector
