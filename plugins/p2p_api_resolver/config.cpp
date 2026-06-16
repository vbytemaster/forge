module;

#include <fcl/exceptions/macros.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

module fcl.plugins.p2p_api_resolver.plugin;

import fcl.api.transport.options;
import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.plugins.p2p_api_resolver.exceptions;
import fcl.plugins.p2p_api_resolver.types;

#include "details/config.hxx"

namespace fcl::plugins::p2p_api_resolver {
namespace {

[[nodiscard]] bool valid_protocol(std::string_view value) noexcept {
   return !value.empty() && value.front() == '/';
}

} // namespace

std::chrono::milliseconds to_ms(std::uint64_t value) {
   return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value)};
}

config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      auto message = std::string{"invalid P2P API resolver config"};
      if (!decoded.diagnostics.entries.empty()) {
         const auto& first = decoded.diagnostics.entries.front();
         message += ": " + first.path + " " + first.code + " " + first.message;
      }
      FCL_THROW_EXCEPTION(exceptions::invalid_config, message);
   }
   return std::move(decoded.value);
}

void validate_config(const config& value) {
   if (!valid_protocol(value.protocol_id)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "resolver protocol id is invalid",
                          fcl::exceptions::ctx("protocol", value.protocol_id));
   }
   if (value.cache_ttl_ms == 0 || value.query_deadline_ms == 0 || value.open_deadline_ms == 0 ||
       value.max_cached_peers == 0 || value.max_apis_per_peer == 0 || value.max_methods_per_api == 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "resolver limits must be positive");
   }
}

void validate_transport_options(const fcl::api::transport::options& value) {
   if (value.codec.value.empty() || value.max_inflight == 0 || value.max_frame_size == 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "resolver API transport options are invalid");
   }
}

} // namespace fcl::plugins::p2p_api_resolver
