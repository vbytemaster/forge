module;

#include <optional>
#include <set>
#include <utility>
#include <vector>

module fcl.p2p.node;

import fcl.p2p.endpoint;
import fcl.p2p.identity;

#include "host_addresses.hpp"

namespace fcl::p2p::host_addresses {

std::vector<endpoint> merge_advertised(const std::vector<endpoint>& configured,
                                       const std::vector<endpoint>& listened, const peer_id& local) {
   auto out = std::vector<endpoint>{};
   auto seen = std::set<std::string>{};
   const auto append = [&](endpoint value) {
      value.peer = local;
      const auto key = value.to_string();
      if (seen.insert(key).second) {
         out.push_back(std::move(value));
      }
   };
   for (const auto& endpoint : configured) {
      append(endpoint);
   }
   for (const auto& endpoint : listened) {
      append(endpoint);
   }
   return out;
}

std::optional<endpoint> learned(endpoint value, const peer_id& peer) {
   if (value.peer && value.peer->to_bytes() != peer.to_bytes()) {
      return std::nullopt;
   }
   value.peer = peer;
   return value;
}

} // namespace fcl::p2p::host_addresses
