#pragma once

namespace fcl::plugins::http::server {

[[nodiscard]] fcl::http::middleware_descriptor to_http_middleware(middleware_descriptor descriptor);

} // namespace fcl::plugins::http::server
