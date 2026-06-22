module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

module fcl.plugins.p2p_api_resolver.plugin;

import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.transport.options;
import fcl.api.types;
import fcl.exceptions;
import fcl.p2p.protocol;
import fcl.plugins.p2p_api_resolver.exceptions;
import fcl.plugins.p2p_api_resolver.types;

#include "details/config.hxx"
#include "details/descriptor_projection.hxx"

namespace fcl::plugins::p2p_api_resolver {
namespace {

[[nodiscard]] bool valid_protocol(std::string_view value) noexcept {
   return !value.empty() && value.front() == '/';
}

[[nodiscard]] std::string entry_key(const entry& value) {
   return api_key(value.id, value.version.major) + "#" + std::to_string(value.version.revision);
}

[[nodiscard]] error project_error(const fcl::api::error_descriptor& value) {
   return error{
      .name = value.name,
      .identity = value.identity,
      .status_code = value.status_code,
      .retryable = value.retryable,
   };
}

[[nodiscard]] method project_method(const fcl::api::method_descriptor& value) {
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

std::string api_key(const fcl::api::api_id& id, std::uint16_t major) {
   return id.value + "#" + std::to_string(major);
}

entry project_descriptor(const fcl::api::descriptor& descriptor,
                         const fcl::p2p::protocol_id& protocol,
                         const fcl::api::transport::options& options) {
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
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "resolver API entry is invalid",
                          fcl::exceptions::ctx("source", source), fcl::exceptions::ctx("api", value.id.value),
                          fcl::exceptions::ctx("protocol", value.protocol));
   }
   if (value.codec.value.empty() || value.max_inflight == 0 || value.max_frame_size == 0) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "resolver API entry limits are invalid",
                          fcl::exceptions::ctx("source", source), fcl::exceptions::ctx("api", value.id.value));
   }
   if (value.max_frame_size > (std::numeric_limits<std::uint32_t>::max)()) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error,
                          "resolver API max frame size exceeds transport limit",
                          fcl::exceptions::ctx("source", source), fcl::exceptions::ctx("api", value.id.value));
   }
   if (value.methods.size() > limits.max_methods_per_api) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "resolver API method limit exceeded",
                          fcl::exceptions::ctx("source", source), fcl::exceptions::ctx("api", value.id.value));
   }
   auto method_names = std::set<std::string>{};
   for (const auto& method : value.methods) {
      if (method.name.empty() || !method_names.insert(method.name).second) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "resolver API method is invalid",
                             fcl::exceptions::ctx("source", source), fcl::exceptions::ctx("api", value.id.value));
      }
      if (method.errors.size() > limits.max_errors_per_method) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "resolver API error limit exceeded",
                             fcl::exceptions::ctx("source", source), fcl::exceptions::ctx("api", value.id.value),
                             fcl::exceptions::ctx("method", method.name));
      }
   }
}

void validate_response(const std::vector<entry>& entries, const config& limits) {
   if (entries.size() > limits.max_apis_per_peer) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "resolver API response limit exceeded");
   }
   auto keys = std::set<std::string>{};
   for (const auto& entry : entries) {
      validate_entry(entry, limits, "remote");
      if (!keys.insert(entry_key(entry)).second) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error,
                             "resolver API response contains duplicate entry",
                             fcl::exceptions::ctx("api", entry.id.value));
      }
   }
}

void validate_descriptor_compatible(const fcl::api::descriptor& descriptor, const entry& remote) {
   if (!fcl::api::compatible(fcl::api::descriptor{.id = remote.id, .version = remote.version},
                             fcl::api::api_ref{.id = descriptor.id,
                                               .major = descriptor.version.major,
                                               .min_revision = descriptor.version.revision})) {
      FCL_THROW_EXCEPTION(exceptions::incompatible_api, "remote API version is incompatible",
                          fcl::exceptions::ctx("api", descriptor.id.value));
   }
   for (const auto& local_method : descriptor.methods) {
      const auto found = std::ranges::find_if(remote.methods, [&](const auto& candidate) {
         return fcl::api::compatible(fcl::api::method_descriptor{.name = candidate.name, .kind = candidate.kind},
                                     local_method);
      });
      if (found == remote.methods.end()) {
         FCL_THROW_EXCEPTION(exceptions::incompatible_api, "remote API method is incompatible",
                             fcl::exceptions::ctx("api", descriptor.id.value),
                             fcl::exceptions::ctx("method", local_method.name));
      }
   }
}

std::optional<entry> select_compatible(const std::vector<entry>& entries,
                                       const fcl::api::api_ref& requested) {
   auto selected = std::optional<entry>{};
   for (const auto& entry : entries) {
      if (!fcl::api::compatible(fcl::api::descriptor{.id = entry.id, .version = entry.version}, requested)) {
         continue;
      }
      if (!selected || entry.version.revision > selected->version.revision) {
         selected = entry;
      }
   }
   return selected;
}

} // namespace fcl::plugins::p2p_api_resolver
