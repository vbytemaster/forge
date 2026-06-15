#pragma once

namespace fcl::plugins::http_server::detail {

[[nodiscard]] std::string normalize_base_path(std::string_view value, std::string_view field);

} // namespace fcl::plugins::http_server::detail
