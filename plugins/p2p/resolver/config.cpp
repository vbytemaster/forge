module;

#include <forge/exceptions/macros.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

module forge.plugins.p2p.resolver.plugin;

import forge.transport.api.options;
import forge.config.component;
import forge.config.decode;
import forge.exceptions;
import forge.plugins.p2p.resolver.exceptions;
import forge.plugins.p2p.resolver.types;

#include "details/config.hxx"

namespace forge::plugins::p2p::resolver {
namespace {

[[nodiscard]] bool valid_protocol(std::string_view value) noexcept {
   return !value.empty() && value.front() == '/';
}

} // namespace

std::chrono::milliseconds to_ms(std::uint64_t value) {
   return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value)};
}

config decode_config(const forge::config::component_view& view) {
   auto decoded = forge::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                          forge::config::format_decode_diagnostics("invalid P2P API resolver config",
                                                                 decoded.diagnostics));
   }
   return std::move(decoded.value);
}

void validate_config(const config& value) {
   if (!valid_protocol(value.protocol_id)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "resolver protocol id is invalid",
                          forge::exceptions::ctx("protocol", value.protocol_id));
   }
}

void validate_transport_options(const forge::transport::api::options& value) {
   if (value.codec.value.empty() || value.max_inflight == 0 || value.max_frame_size == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "resolver API transport options are invalid");
   }
}

} // namespace forge::plugins::p2p::resolver
