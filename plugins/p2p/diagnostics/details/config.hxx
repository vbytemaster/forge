#pragma once

namespace fcl::plugins::p2p::diagnostics {

[[nodiscard]] config decode_config(const fcl::config::component_view& view);
void validate_config(const config& value);
[[nodiscard]] fcl::p2p::diagnostics::options configured_options(const config& value);
[[nodiscard]] std::vector<fcl::p2p::diagnostics::peer>
filter_peers(const fcl::p2p::diagnostics::snapshot& snapshot, const filter& filter);

} // namespace fcl::plugins::p2p::diagnostics
