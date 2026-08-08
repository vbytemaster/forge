module;

#include <forge/api/core/macros.hpp>
#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.p2p.resolver.plugin;

import forge.api.core.binding;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.registry;
import forge.api.core.types;
import forge.api.transport.options;
import forge.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;
import forge.plugins.p2p.resolver.exceptions;
import forge.plugins.p2p.resolver.types;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.exceptions;
import forge.plugins.p2p.node.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/resolver_protocol.hxx"

FORGE_API(::forge::plugins::p2p::resolver::detail::resolver_protocol,
          FORGE_API_CONTRACT("forge.plugins.p2p.resolver.protocol", 1, 0),
          FORGE_API_METHOD(query))

namespace forge::plugins::p2p::resolver {
namespace {

inline constexpr auto resolver_api_id = "forge.plugins.p2p.resolver.protocol";

[[nodiscard]] bool valid_protocol(std::string_view value) noexcept {
   return !value.empty() && value.front() == '/';
}

[[nodiscard]] error project_error(const forge::api::core::error_descriptor& value) {
   return error{
      .name = value.name,
      .identity = value.identity,
      .status_code = value.status_code,
      .retryable = value.retryable,
   };
}

[[nodiscard]] method project_method(const forge::api::core::method_descriptor& value) {
   auto errors = std::vector<error>{};
   errors.reserve(value.errors.size());
   for (const auto& error : value.errors) {
      errors.push_back(project_error(error));
   }
   return method{.name = value.name, .kind = value.kind, .errors = std::move(errors)};
}

} // namespace

forge::plugins::p2p::node::api& plugin::impl::require_p2p() const {
   if (!initialized || p2p == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P API resolver plugin is not initialized");
   }
   return *p2p;
}

std::chrono::milliseconds plugin::impl::query_deadline(resolve_options value) const {
   return value.query_deadline.count() > 0 ? value.query_deadline : to_ms(settings.query_deadline_ms);
}

std::chrono::milliseconds plugin::impl::open_deadline(resolve_options value) const {
   return value.open_deadline.count() > 0 ? value.open_deadline : to_ms(settings.open_deadline_ms);
}

std::chrono::milliseconds plugin::impl::request_deadline(resolve_options value) const {
   return value.request_deadline.count() > 0 ? value.request_deadline : to_ms(settings.request_deadline_ms);
}

void plugin::impl::evict_cache_locked() {
   while (cache.size() > settings.max_cached_peers) {
      auto expired = std::ranges::find_if(cache, [](const auto& item) {
         return item.second.expires_at <= std::chrono::steady_clock::now();
      });
      if (expired != cache.end()) {
         cache.erase(expired);
         continue;
      }
      auto oldest = cache.begin();
      for (auto iterator = cache.begin(); iterator != cache.end(); ++iterator) {
         if (iterator->second.stored_at < oldest->second.stored_at) {
            oldest = iterator;
         }
      }
      cache.erase(oldest);
   }
}

std::optional<std::vector<entry>> plugin::impl::cached_peer(const forge::net::p2p::peer_id& peer,
                                                           resolve_options options) const {
   if (options.force_refresh) {
      return std::nullopt;
   }
   const auto now = std::chrono::steady_clock::now();
   const auto key = peer.to_string();
   auto lock = std::scoped_lock{mutex};
   const auto found = cache.find(key);
   if (found == cache.end() || found->second.expires_at <= now) {
      return std::nullopt;
   }
   return found->second.apis;
}

void plugin::impl::store_peer(const forge::net::p2p::peer_id& peer, std::vector<entry> entries) {
   const auto now = std::chrono::steady_clock::now();
   auto lock = std::scoped_lock{mutex};
   cache[peer.to_string()] = cache_record{
      .apis = std::move(entries),
      .expires_at = now + to_ms(settings.cache_ttl_ms),
      .stored_at = now,
   };
   evict_cache_locked();
}

std::vector<entry> plugin::impl::local_snapshot() const {
   auto lock = std::scoped_lock{mutex};
   return local;
}

void plugin::impl::add_local(forge::api::core::binding_plan plan, forge::net::p2p::protocol_id route, publish_options options) {
   auto& p2p_api = require_p2p();
   validate_transport_options(options.transport);
   if (route.value.empty() || route.value.front() != '/' || plan.exports.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::duplicate_api, "resolver API publication is invalid",
                          forge::exceptions::ctx("protocol", route.value));
   }

   auto projected = std::vector<entry>{};
   projected.reserve(plan.exports.size());
   for (const auto& descriptor : plan.exports) {
      projected.push_back(project_descriptor(descriptor, route, options.transport));
   }
   for (const auto& value : projected) {
      validate_entry(value, "local");
   }

   {
      auto lock = std::scoped_lock{mutex};
      if (local.size() + projected.size() > settings.max_apis_per_peer) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver local API limit exceeded");
      }
      auto keys = std::set<std::string>{};
      auto protocols = std::set<std::string>{};
      for (const auto& value : local) {
         keys.insert(api_key(value.id, value.version.major));
         protocols.insert(value.protocol);
      }
      for (const auto& value : projected) {
         if (!keys.insert(api_key(value.id, value.version.major)).second) {
            FORGE_THROW_EXCEPTION(exceptions::duplicate_api, "duplicate resolver API publication",
                                forge::exceptions::ctx("api", value.id.value));
         }
      }
      if (!protocols.insert(route.value).second) {
         FORGE_THROW_EXCEPTION(exceptions::duplicate_api, "duplicate resolver API protocol",
                             forge::exceptions::ctx("protocol", route.value));
      }
   }

   try {
      p2p_api.publish_api(std::move(plan), route, options.transport);
   } catch (const forge::plugins::p2p::node::exceptions::route_conflict& error) {
      FORGE_THROW_EXCEPTION(exceptions::duplicate_api, "P2P API route conflicts with resolver publication",
                          forge::exceptions::ctx("protocol", route.value),
                          forge::exceptions::ctx("error", error.message()));
   }

   auto lock = std::scoped_lock{mutex};
   local.insert(local.end(), std::make_move_iterator(projected.begin()), std::make_move_iterator(projected.end()));
}

response plugin::impl::query_local(const query& request) const {
   auto entries = local_snapshot();
   if (!request.apis.empty()) {
      auto filtered = std::vector<entry>{};
      for (const auto& requested : request.apis) {
         if (auto selected = select_compatible(entries, requested)) {
            filtered.push_back(std::move(*selected));
         }
      }
      entries = std::move(filtered);
   }
   validate_response(entries);
   return response{.apis = std::move(entries)};
}

std::string plugin::impl::api_key(const forge::api::core::api_id& id, std::uint16_t major) {
   return id.value + "#" + std::to_string(major);
}

entry plugin::impl::project_descriptor(
   const forge::api::core::descriptor& descriptor,
   const forge::net::p2p::protocol_id& route,
   const forge::api::transport::options& options) const {
   auto methods = std::vector<method>{};
   methods.reserve(descriptor.methods.size());
   for (const auto& method : descriptor.methods) {
      methods.push_back(project_method(method));
   }
   return entry{
      .id = descriptor.id,
      .version = descriptor.version,
      .protocol = route.value,
      .codec = options.codec,
      .max_inflight = static_cast<std::uint64_t>(options.max_inflight),
      .max_frame_size = options.max_frame_size,
      .methods = std::move(methods),
   };
}

void plugin::impl::validate_entry(const entry& value, std::string_view source) const {
   if (value.id.value.empty() || value.version.major == 0 || !valid_protocol(value.protocol)) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API entry is invalid",
                            forge::exceptions::ctx("source", source),
                            forge::exceptions::ctx("api", value.id.value),
                            forge::exceptions::ctx("protocol", value.protocol));
   }
   if (value.codec.value.empty() || value.max_inflight == 0 || value.max_frame_size == 0) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API entry limits are invalid",
                            forge::exceptions::ctx("source", source),
                            forge::exceptions::ctx("api", value.id.value));
   }
   if (value.max_frame_size > (std::numeric_limits<std::uint32_t>::max)()) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "resolver API max frame size exceeds transport limit",
                            forge::exceptions::ctx("source", source),
                            forge::exceptions::ctx("api", value.id.value));
   }
   if (value.methods.size() > settings.max_methods_per_api) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API method limit exceeded",
                            forge::exceptions::ctx("source", source),
                            forge::exceptions::ctx("api", value.id.value));
   }
   auto method_names = std::set<std::string>{};
   for (const auto& method : value.methods) {
      if (method.name.empty() || !method_names.insert(method.name).second) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API method is invalid",
                               forge::exceptions::ctx("source", source),
                               forge::exceptions::ctx("api", value.id.value));
      }
      if (method.errors.size() > settings.max_errors_per_method) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API error limit exceeded",
                               forge::exceptions::ctx("source", source),
                               forge::exceptions::ctx("api", value.id.value),
                               forge::exceptions::ctx("method", method.name));
      }
   }
}

void plugin::impl::validate_response(const std::vector<entry>& entries) const {
   if (entries.size() > settings.max_apis_per_peer) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "resolver API response limit exceeded");
   }
   auto keys = std::set<std::string>{};
   for (const auto& value : entries) {
      validate_entry(value, "remote");
      const auto key = api_key(value.id, value.version.major) + "#" +
                       std::to_string(value.version.revision);
      if (!keys.insert(key).second) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                               "resolver API response contains duplicate entry",
                               forge::exceptions::ctx("api", value.id.value));
      }
   }
}

void plugin::impl::validate_descriptor_compatible(
   const forge::api::core::descriptor& descriptor,
   const entry& remote) const {
   if (!forge::api::core::compatible(
          forge::api::core::descriptor{.id = remote.id, .version = remote.version},
          forge::api::core::api_ref{
             .id = descriptor.id,
             .major = descriptor.version.major,
             .min_revision = descriptor.version.revision,
          })) {
      FORGE_THROW_EXCEPTION(exceptions::incompatible_api, "remote API version is incompatible",
                            forge::exceptions::ctx("api", descriptor.id.value));
   }
   for (const auto& local_method : descriptor.methods) {
      const auto found = std::ranges::find_if(remote.methods, [&](const auto& candidate) {
         return forge::api::core::compatible(
            forge::api::core::method_descriptor{.name = candidate.name, .kind = candidate.kind},
            local_method);
      });
      if (found == remote.methods.end()) {
         FORGE_THROW_EXCEPTION(exceptions::incompatible_api, "remote API method is incompatible",
                               forge::exceptions::ctx("api", descriptor.id.value),
                               forge::exceptions::ctx("method", local_method.name));
      }
   }
}

std::optional<entry> plugin::impl::select_compatible(
   const std::vector<entry>& entries,
   const forge::api::core::api_ref& requested) const {
   auto selected = std::optional<entry>{};
   for (const auto& value : entries) {
      if (!forge::api::core::compatible(
             forge::api::core::descriptor{.id = value.id, .version = value.version},
             requested)) {
         continue;
      }
      if (!selected || value.version.revision > selected->version.revision) {
         selected = value;
      }
   }
   return selected;
}

void plugin::impl::install_protocol() {
   protocol_registry.clear();
   protocol_registry.install<detail::resolver_protocol>(
      std::make_shared<detail::resolver_protocol>(shared_from_this()));
   auto plan = forge::api::core::binding()
                  .serve(protocol_registry)
                  .export_api<detail::resolver_protocol>(
                     {.id = {resolver_api_id}, .major = 1, .min_revision = 0})
                  .build();
   p2p->publish_api(std::move(plan), protocol, resolver_transport);
}

boost::asio::awaitable<std::vector<entry>>
plugin::impl::query_remote_apis(forge::net::p2p::peer_id peer, resolve_options options) {
   auto remote = co_await p2p->remote<detail::resolver_protocol>(
      peer, protocol,
      forge::plugins::p2p::node::remote_options{
         .open_deadline = open_deadline(options),
         .deadline = query_deadline(options),
      });
   auto result = co_await remote->query(query{});
   co_return std::move(result.apis);
}

} // namespace forge::plugins::p2p::resolver
