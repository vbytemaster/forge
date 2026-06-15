module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>
#include <string>

module fcl.plugins.http_server.plugin;

import fcl.config.component;
import fcl.exceptions;
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.types;

#include "private/base_path.hxx"
#include "private/config_decode.hxx"

namespace fcl::plugins::http_server::detail {

config decode_config(const fcl::config::component_view& view) {
   auto result = config{};
   result.bind_address = view.get_or<std::string>("bind-address", result.bind_address);
   result.port = view.get_or<std::uint64_t>("port", result.port);
   result.api_base_path = view.get_or<std::string>("api-base-path", result.api_base_path);
   result.max_request_body_bytes =
      view.get_or<std::uint64_t>("max-request-body-bytes", result.max_request_body_bytes);
   result.max_header_bytes = view.get_or<std::uint64_t>("max-header-bytes", result.max_header_bytes);
   result.read_timeout_ms = view.get_or<std::uint64_t>("read-timeout-ms", result.read_timeout_ms);
   result.idle_timeout_ms = view.get_or<std::uint64_t>("idle-timeout-ms", result.idle_timeout_ms);

   if (result.port == 0 || result.port > 65535) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "HTTP server port is out of range");
   }
   if (result.max_request_body_bytes == 0 || result.max_header_bytes == 0 || result.read_timeout_ms == 0 ||
       result.idle_timeout_ms == 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "HTTP server limits and timeouts must be positive");
   }
   result.api_base_path = normalize_base_path(result.api_base_path, "api-base-path");
   if (result.api_base_path.empty()) {
      result.api_base_path = "/";
   }
   return result;
}

} // namespace fcl::plugins::http_server::detail
