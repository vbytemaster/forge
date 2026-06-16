#pragma once

namespace fcl::plugins::p2p_node {

[[nodiscard]] fcl::p2p::peer_id default_test_peer();
[[nodiscard]] std::chrono::milliseconds to_ms(std::uint64_t value);
[[nodiscard]] config decode_config(const fcl::config::component_view& view);
[[nodiscard]] parsed_policy parse_policy(const config& config);
[[nodiscard]] std::vector<fcl::p2p::endpoint> parse_endpoint_list(const std::vector<std::string>& values);
void apply_config(plugin::impl& state, const config& config);

} // namespace fcl::plugins::p2p_node
