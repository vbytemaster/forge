#pragma once

namespace fcl::plugins::http_server::detail {

[[nodiscard]] config decode_config(const fcl::config::component_view& view);

} // namespace fcl::plugins::http_server::detail
