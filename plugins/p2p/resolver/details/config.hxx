#pragma once

namespace forge::plugins::p2p::resolver {

[[nodiscard]] std::chrono::milliseconds to_ms(std::uint64_t value);
[[nodiscard]] config decode_config(const forge::config::component_view& view);
void validate_config(const config& value);
void validate_transport_options(const forge::transport::api::options& value);

} // namespace forge::plugins::p2p::resolver
