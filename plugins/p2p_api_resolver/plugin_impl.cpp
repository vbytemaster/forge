module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_api_resolver.plugin;

import fcl.api.binding;
import fcl.api.registry;
import fcl.api.transport.options;
import fcl.exceptions;
import fcl.p2p.identity;
import fcl.p2p.protocol;
import fcl.plugins.p2p_api_resolver.exceptions;
import fcl.plugins.p2p_api_resolver.types;
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_node.exceptions;

#include "details/config.hxx"
#include "details/descriptor_projection.hxx"
#include "details/plugin_impl.hxx"

namespace fcl::plugins::p2p_api_resolver {

fcl::plugins::p2p_node::api& plugin::impl::require_p2p() const {
   if (!initialized || p2p == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P API resolver plugin is not initialized");
   }
   return *p2p;
}

std::chrono::milliseconds plugin::impl::query_deadline(resolve_options value) const {
   return value.query_deadline.count() > 0 ? value.query_deadline : to_ms(settings.query_deadline_ms);
}

std::chrono::milliseconds plugin::impl::open_deadline(resolve_options value) const {
   return value.open_deadline.count() > 0 ? value.open_deadline : to_ms(settings.open_deadline_ms);
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

std::optional<std::vector<entry>> plugin::impl::cached_peer(const fcl::p2p::peer_id& peer,
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

void plugin::impl::store_peer(const fcl::p2p::peer_id& peer, std::vector<entry> entries) {
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

void plugin::impl::add_local(fcl::api::binding_plan plan, fcl::p2p::protocol_id route, publish_options options) {
   auto& p2p_api = require_p2p();
   validate_transport_options(options.transport);
   if (route.value.empty() || route.value.front() != '/' || plan.exports.empty()) {
      FCL_THROW_EXCEPTION(exceptions::duplicate_api, "resolver API publication is invalid",
                          fcl::exceptions::ctx("protocol", route.value));
   }

   auto projected = std::vector<entry>{};
   projected.reserve(plan.exports.size());
   for (const auto& descriptor : plan.exports) {
      projected.push_back(project_descriptor(descriptor, route, options.transport));
   }
   for (const auto& value : projected) {
      validate_entry(value, settings, "local");
   }

   {
      auto lock = std::scoped_lock{mutex};
      if (local.size() + projected.size() > settings.max_apis_per_peer) {
         FCL_THROW_EXCEPTION(exceptions::protocol_error, "resolver local API limit exceeded");
      }
      auto keys = std::set<std::string>{};
      auto protocols = std::set<std::string>{};
      for (const auto& value : local) {
         keys.insert(api_key(value.id, value.version.major));
         protocols.insert(value.protocol);
      }
      for (const auto& value : projected) {
         if (!keys.insert(api_key(value.id, value.version.major)).second) {
            FCL_THROW_EXCEPTION(exceptions::duplicate_api, "duplicate resolver API publication",
                                fcl::exceptions::ctx("api", value.id.value));
         }
      }
      if (!protocols.insert(route.value).second) {
         FCL_THROW_EXCEPTION(exceptions::duplicate_api, "duplicate resolver API protocol",
                             fcl::exceptions::ctx("protocol", route.value));
      }
   }

   try {
      p2p_api.publish_api(std::move(plan), route, options.transport);
   } catch (const fcl::plugins::p2p_node::exceptions::route_conflict& error) {
      FCL_THROW_EXCEPTION(exceptions::duplicate_api, "P2P API route conflicts with resolver publication",
                          fcl::exceptions::ctx("protocol", route.value),
                          fcl::exceptions::ctx("error", error.message()));
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
   validate_response(entries, settings);
   return response{.apis = std::move(entries)};
}

} // namespace fcl::plugins::p2p_api_resolver
