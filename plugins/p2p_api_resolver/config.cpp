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
      FCL_THROW_EXCEPTION(exceptions::invalid_config,
                          fcl::config::format_decode_diagnostics("invalid P2P API resolver config",
                                                                 decoded.diagnostics));
   }
   return std::move(decoded.value);
}

void validate_config(const config& value) {
   if (!valid_protocol(value.protocol_id)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "resolver protocol id is invalid",
                          fcl::exceptions::ctx("protocol", value.protocol_id));
   }
}

void validate_transport_options(const fcl::api::transport::options& value) {
   if (value.codec.value.empty() || value.max_inflight == 0 || value.max_frame_size == 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "resolver API transport options are invalid");
   }
}

} // namespace fcl::plugins::p2p_api_resolver
