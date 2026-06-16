#pragma once

namespace fcl::plugins::p2p_api_resolver {

[[nodiscard]] std::chrono::milliseconds to_ms(std::uint64_t value);
[[nodiscard]] config decode_config(const fcl::config::component_view& view);
void validate_config(const config& value);
void validate_transport_options(const fcl::api::transport::options& value);

} // namespace fcl::plugins::p2p_api_resolver
