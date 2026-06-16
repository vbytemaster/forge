#pragma once

namespace fcl::plugins::http_server {

[[nodiscard]] config decode_config(const fcl::config::component_view& view);
[[nodiscard]] fcl::http::server_config to_server_config(const config& value);
[[nodiscard]] std::string normalize_base_path(std::string_view value);
[[nodiscard]] std::string resolve_base_path(const config& settings, std::string_view override_value);

} // namespace fcl::plugins::http_server
