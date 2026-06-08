#pragma once

#include <chrono>
#include <vector>

namespace fcl::p2p::path_selector {

[[nodiscard]] std::vector<peer_store::endpoint_record>
rank_direct(const peer_store::record& record, std::chrono::system_clock::time_point now);

} // namespace fcl::p2p::path_selector
