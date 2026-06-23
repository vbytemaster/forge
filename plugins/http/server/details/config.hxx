#pragma once

namespace forge::plugins::http::server {

[[nodiscard]] config decode_config(const forge::config::component_view& view);
[[nodiscard]] forge::http::server_config to_server_config(const config& value);
[[nodiscard]] std::string normalize_base_path(std::string_view value);
[[nodiscard]] std::string resolve_base_path(const config& settings, std::string_view override_value);

} // namespace forge::plugins::http::server
