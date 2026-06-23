module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

module forge.plugins.p2p.resolver.plugin;

import forge.api.descriptor;
import forge.api.error_projection;
import forge.transport.api.options;
import forge.api.types;
import forge.exceptions;
import forge.p2p.protocol;
import forge.plugins.p2p.resolver.exceptions;
import forge.plugins.p2p.resolver.types;

#include "details/config.hxx"
#include "details/descriptor_projection.hxx"

namespace forge::plugins::p2p::resolver {
namespace {

[[nodiscard]] bool valid_protocol(std::string_view value) noexcept {
   return !value.empty() && value.front() == '/';
}

[[nodiscard]] std::string entry_key(const entry& value) {
   return api_key(value.id, value.version.major) + "#" + std::to_string(value.version.revision);
}

[[nodiscard]] error project_error(const forge::api::error_descriptor& value) {
   return error{
      .name = value.name,
      .identity = value.identity,
      .status_code = value.status_code,
      .retryable = value.retryable,
   };
}

[[nodiscard]] method project_method(const forge::api::method_descriptor& value) {
   auto errors = std::vector<error>{};
   errors.reserve(value.errors.size());
   for (const auto& error : value.errors) {
      errors.push_back(project_error(error));
   }
   return method{
      .name = value.name,
      .kind = value.kind,
      .errors = std::move(errors),
   };
}

} // namespace

std::string api_key(const forge::api::api_id& id, std::uint16_t major) {
   return id.value + "#" + std::to_string(major);
}

entry project_descriptor(const forge::api::descriptor& descriptor,
                         const forge::p2p::protocol_id& protocol,
                         const forge::transport::api::options& options) {
   auto methods = std::vector<method>{};
   methods.reserve(descriptor.methods.size());
   for (const auto& method : descriptor.methods) {
      methods.push_back(project_method(method));
   }
   return entry{
      .id = descriptor.id,
      .version = descriptor.version,
      .protocol = protocol.value,
      .codec = options.codec,
      .max_inflight = static_cast<std::uint64_t>(options.max_inflight),
      .max_frame_size = options.max_frame_size,
      .methods = std::move(methods),
   };
}

void validate_entry(const entry& value, const config& limits, std::string_view source) {
   if (value.id.value.empty() || value.version.major == 0 || !valid_protocol(value.protocol)) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API entry is invalid",
                          forge::exceptions::ctx("source", source), forge::exceptions::ctx("api", value.id.value),
                          forge::exceptions::ctx("protocol", value.protocol));
   }
   if (value.codec.value.empty() || value.max_inflight == 0 || value.max_frame_size == 0) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API entry limits are invalid",
                          forge::exceptions::ctx("source", source), forge::exceptions::ctx("api", value.id.value));
   }
   if (value.max_frame_size > (std::numeric_limits<std::uint32_t>::max)()) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                          "resolver API max frame size exceeds transport limit",
                          forge::exceptions::ctx("source", source), forge::exceptions::ctx("api", value.id.value));
   }
   if (value.methods.size() > limits.max_methods_per_api) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API method limit exceeded",
                          forge::exceptions::ctx("source", source), forge::exceptions::ctx("api", value.id.value));
   }
   auto method_names = std::set<std::string>{};
   for (const auto& method : value.methods) {
      if (method.name.empty() || !method_names.insert(method.name).second) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API method is invalid",
                             forge::exceptions::ctx("source", source), forge::exceptions::ctx("api", value.id.value));
      }
      if (method.errors.size() > limits.max_errors_per_method) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API error limit exceeded",
                             forge::exceptions::ctx("source", source), forge::exceptions::ctx("api", value.id.value),
                             forge::exceptions::ctx("method", method.name));
      }
   }
}

void validate_response(const std::vector<entry>& entries, const config& limits) {
   if (entries.size() > limits.max_apis_per_peer) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API response limit exceeded");
   }
   auto keys = std::set<std::string>{};
   for (const auto& entry : entries) {
      validate_entry(entry, limits, "remote");
      if (!keys.insert(entry_key(entry)).second) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                             "resolver API response contains duplicate entry",
                             forge::exceptions::ctx("api", entry.id.value));
      }
   }
}

void validate_descriptor_compatible(const forge::api::descriptor& descriptor, const entry& remote) {
   if (!forge::api::compatible(forge::api::descriptor{.id = remote.id, .version = remote.version},
                             forge::api::api_ref{.id = descriptor.id,
                                               .major = descriptor.version.major,
                                               .min_revision = descriptor.version.revision})) {
      FORGE_THROW_EXCEPTION(exceptions::incompatible_api, "remote API version is incompatible",
                          forge::exceptions::ctx("api", descriptor.id.value));
   }
   for (const auto& local_method : descriptor.methods) {
      const auto found = std::ranges::find_if(remote.methods, [&](const auto& candidate) {
         return forge::api::compatible(forge::api::method_descriptor{.name = candidate.name, .kind = candidate.kind},
                                     local_method);
      });
      if (found == remote.methods.end()) {
         FORGE_THROW_EXCEPTION(exceptions::incompatible_api, "remote API method is incompatible",
                             forge::exceptions::ctx("api", descriptor.id.value),
                             forge::exceptions::ctx("method", local_method.name));
      }
   }
}

std::optional<entry> select_compatible(const std::vector<entry>& entries,
                                       const forge::api::api_ref& requested) {
   auto selected = std::optional<entry>{};
   for (const auto& entry : entries) {
      if (!forge::api::compatible(forge::api::descriptor{.id = entry.id, .version = entry.version}, requested)) {
         continue;
      }
      if (!selected || entry.version.revision > selected->version.revision) {
         selected = entry;
      }
   }
   return selected;
}

} // namespace forge::plugins::p2p::resolver
