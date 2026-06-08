#pragma once

#include <optional>
#include <vector>

namespace fcl::p2p::host_addresses {

enum class source_kind {
   authenticated,
   third_party,
};

struct learning_context {
   source_kind source = source_kind::third_party;
   std::optional<fcl::p2p::endpoint> remote_endpoint;
};

[[nodiscard]] std::vector<fcl::p2p::endpoint> merge_advertised(const std::vector<fcl::p2p::endpoint>& configured,
                                                               const std::vector<fcl::p2p::endpoint>& listened,
                                                               const peer_id& local);

[[nodiscard]] std::optional<fcl::p2p::endpoint> learned(fcl::p2p::endpoint value, const peer_id& peer);
[[nodiscard]] std::optional<fcl::p2p::endpoint> learned(fcl::p2p::endpoint value, const peer_id& peer,
                                                        learning_context context);
[[nodiscard]] std::vector<fcl::p2p::endpoint>
sanitize_discovered_endpoints(std::vector<fcl::p2p::endpoint> values, const peer_id& peer, learning_context context);

} // namespace fcl::p2p::host_addresses
