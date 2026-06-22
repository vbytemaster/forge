module;

#include <fcl/exceptions/macros.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

module fcl.plugins.http.server.plugin;

import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.http.server;
import fcl.plugins.http.server.exceptions;
import fcl.plugins.http.server.types;

#include "details/config.hxx"

namespace fcl::plugins::http::server {

config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config,
                          fcl::config::format_decode_diagnostics("invalid HTTP server config",
                                                                 decoded.diagnostics));
   }
   decoded.value.api_base_path = normalize_base_path(decoded.value.api_base_path);
   return std::move(decoded.value);
}

std::string normalize_base_path(std::string_view value) {
   if (value.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "HTTP server API base path must not be empty");
   }
   if (value.front() != '/') {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "HTTP server API base path must start with /",
                          fcl::exceptions::ctx("base_path", std::string{value}));
   }
   while (value.size() > 1U && value.back() == '/') {
      value.remove_suffix(1U);
   }
   return std::string{value};
}

std::string resolve_base_path(const config& settings, std::string_view override_value) {
   if (!override_value.empty()) {
      return normalize_base_path(override_value);
   }
   return normalize_base_path(settings.api_base_path);
}

fcl::http::server_config to_server_config(const config& value) {
   return fcl::http::server_config{
      .bind_address = value.bind_address,
      .port = static_cast<std::uint16_t>(value.port),
      .max_request_body_bytes = value.max_request_body_bytes,
      .max_header_bytes = value.max_header_bytes,
      .read_timeout = std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value.read_timeout_ms)},
      .idle_timeout = std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value.idle_timeout_ms)},
   };
}

} // namespace fcl::plugins::http::server
