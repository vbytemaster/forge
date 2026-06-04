#pragma once

#include <optional>
#include <vector>

namespace fcl::p2p::host_addresses {

[[nodiscard]] std::vector<fcl::p2p::endpoint>
merge_advertised(const std::vector<fcl::p2p::endpoint>& configured,
                 const std::vector<fcl::p2p::endpoint>& listened, const peer_id& local);

[[nodiscard]] std::optional<fcl::p2p::endpoint> learned(fcl::p2p::endpoint value, const peer_id& peer);

} // namespace fcl::p2p::host_addresses
