#pragma once

namespace fcl::plugins::p2p::pubsub {

[[nodiscard]] std::chrono::milliseconds to_ms(std::uint64_t value);
[[nodiscard]] config decode_config(const fcl::config::component_view& view);
void validate_config(const config& value);
[[nodiscard]] fcl::p2p::pubsub::options core_options_for(const config& settings);

} // namespace fcl::plugins::p2p::pubsub
