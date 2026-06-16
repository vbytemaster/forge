module;

#include <fcl/api/macros.hpp>
#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module fcl.plugins.p2p_api_resolver.plugin;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.api.transport.exceptions;
import fcl.api.transport.options;
import fcl.api.transport.client;
import fcl.api.transport.connection;
import fcl.api.transport.server;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.p2p.exceptions;
import fcl.p2p.identity;
import fcl.p2p.endpoint;
import fcl.p2p.envelope;
import fcl.p2p.identify;
import fcl.p2p.diagnostics;
import fcl.p2p.discovery;
import fcl.p2p.dht;
import fcl.p2p.rendezvous;
import fcl.p2p.pubsub;
import fcl.p2p.reachability;
import fcl.p2p.hole_punch;
import fcl.p2p.protocol;
import fcl.p2p.message;
import fcl.p2p.scoring;
import fcl.p2p.relay;
import fcl.p2p.resource_manager;
import fcl.p2p.stream;
import fcl.p2p.negotiation;
import fcl.p2p.peer_store;
import fcl.p2p.node;
import fcl.p2p.api;
import fcl.plugins.p2p_node.types;
import fcl.plugins.p2p_node.exceptions;
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_api_resolver.api;
import fcl.plugins.p2p_api_resolver.exceptions;
import fcl.plugins.p2p_api_resolver.types;

namespace fcl::plugins::p2p_api_resolver::detail {

class resolver_protocol
    : public fcl::api::contract<resolver_protocol, fcl::api::surface::local | fcl::api::surface::remote> {
 public:
   virtual ~resolver_protocol() = default;
   virtual boost::asio::awaitable<response> query(::fcl::plugins::p2p_api_resolver::query request) = 0;
};

} // namespace fcl::plugins::p2p_api_resolver::detail

FCL_API(::fcl::plugins::p2p_api_resolver::detail::resolver_protocol,
        FCL_API_CONTRACT("fcl.plugins.p2p_api_resolver.protocol", 1, 0), FCL_API_METHOD(query))

namespace fcl::plugins::p2p_api_resolver {
namespace {

constexpr auto resolver_api_id = "fcl.plugins.p2p_api_resolver.protocol";

[[nodiscard]] std::chrono::milliseconds to_ms(std::uint64_t value) {
   return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value)};
}

[[nodiscard]] std::string api_key(const fcl::api::api_id& id, std::uint16_t major) {
   return id.value + "#" + std::to_string(major);
}

[[nodiscard]] std::string entry_key(const entry& value) {
   return api_key(value.id, value.version.major) + "#" + std::to_string(value.version.revision);
}

[[nodiscard]] bool valid_protocol(std::string_view value) noexcept {
   return !value.empty() && value.front() == '/';
}

[[nodiscard]] config decode_config(const fcl::config::component_view& view) {
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

[[nodiscard]] entry project_descriptor(const fcl::api::descriptor& descriptor,
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

void validate_entry(const entry& value, const config& limits,
                    std::string_view source) {
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

[[nodiscard]] bool method_compatible(const fcl::api::method_descriptor& local,
                                     const method& remote) noexcept {
   return local.name == remote.name && local.kind == remote.kind;
}

void validate_descriptor_compatible(const fcl::api::descriptor& descriptor, const entry& remote) {
   if (descriptor.id != remote.id || descriptor.version.major != remote.version.major ||
       descriptor.version.revision > remote.version.revision) {
      FCL_THROW_EXCEPTION(exceptions::incompatible_api, "remote API version is incompatible",
                          fcl::exceptions::ctx("api", descriptor.id.value));
   }
   for (const auto& local_method : descriptor.methods) {
      const auto found = std::ranges::find_if(remote.methods, [&](const auto& candidate) {
         return method_compatible(local_method, candidate);
      });
      if (found == remote.methods.end()) {
         FCL_THROW_EXCEPTION(exceptions::incompatible_api, "remote API method is incompatible",
                             fcl::exceptions::ctx("api", descriptor.id.value),
                             fcl::exceptions::ctx("method", local_method.name));
      }
   }
}

[[nodiscard]] std::optional<entry>
select_compatible(const std::vector<entry>& entries, const fcl::api::api_ref& requested) {
   auto selected = std::optional<entry>{};
   for (const auto& entry : entries) {
      if (entry.id != requested.id || entry.version.major != requested.major ||
          entry.version.revision < requested.min_revision) {
         continue;
      }
      if (!selected || entry.version.revision > selected->version.revision) {
         selected = entry;
      }
   }
   return selected;
}

} // namespace

struct plugin::impl : public std::enable_shared_from_this<plugin::impl> {
   struct cache_record {
      std::vector<entry> apis;
      std::chrono::steady_clock::time_point expires_at;
      std::chrono::steady_clock::time_point stored_at;
   };

   mutable std::mutex mutex;
   config settings;
   fcl::api::transport::options resolver_transport{};
   fcl::p2p::protocol_id protocol = default_protocol();
   fcl::plugins::p2p_node::api* p2p = nullptr;
   fcl::api::registry protocol_registry;
   std::vector<entry> local;
   std::map<std::string, cache_record> cache;
   bool initialized = false;
   bool stopping = false;

   [[nodiscard]] fcl::plugins::p2p_node::api& require_p2p() const {
      if (!initialized || p2p == nullptr) {
         FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P API resolver plugin is not initialized");
      }
      return *p2p;
   }

   [[nodiscard]] std::chrono::milliseconds query_deadline(resolve_options value) const {
      return value.query_deadline.count() > 0 ? value.query_deadline : to_ms(settings.query_deadline_ms);
   }

   [[nodiscard]] std::chrono::milliseconds open_deadline(resolve_options value) const {
      return value.open_deadline.count() > 0 ? value.open_deadline : to_ms(settings.open_deadline_ms);
   }

   void evict_cache_locked() {
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

   [[nodiscard]] std::optional<std::vector<entry>> cached_peer(const fcl::p2p::peer_id& peer,
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

   void store_peer(const fcl::p2p::peer_id& peer, std::vector<entry> entries) {
      const auto now = std::chrono::steady_clock::now();
      auto lock = std::scoped_lock{mutex};
      cache[peer.to_string()] = cache_record{
         .apis = std::move(entries),
         .expires_at = now + to_ms(settings.cache_ttl_ms),
         .stored_at = now,
      };
      evict_cache_locked();
   }

   [[nodiscard]] std::vector<entry> local_snapshot() const {
      auto lock = std::scoped_lock{mutex};
      return local;
   }

   void add_local(fcl::api::binding_plan plan, fcl::p2p::protocol_id route, publish_options options) {
      auto& p2p_api = require_p2p();
      validate_transport_options(options.transport);
      if (!valid_protocol(route.value) || plan.exports.empty()) {
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

   [[nodiscard]] response query_local(const query& request) const {
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
};

class plugin::protocol_impl final : public detail::resolver_protocol {
 public:
   explicit protocol_impl(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

   boost::asio::awaitable<response> query(::fcl::plugins::p2p_api_resolver::query request) override {
      co_return impl_->query_local(request);
   }

 private:
   std::shared_ptr<plugin::impl> impl_;
};

class plugin::api_impl final : public api {
 public:
   explicit api_impl(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

   void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol, publish_options options) override {
      impl_->add_local(std::move(plan), std::move(protocol), std::move(options));
   }

   std::vector<entry> local_apis() const override {
      (void)impl_->require_p2p();
      return impl_->local_snapshot();
   }

   boost::asio::awaitable<std::vector<entry>> peer_apis(fcl::p2p::peer_id peer, resolve_options options) override {
      (void)impl_->require_p2p();
      if (auto cached = impl_->cached_peer(peer, options)) {
         co_return *cached;
      }

      auto remote = co_await impl_->p2p->remote<detail::resolver_protocol>(
         peer, impl_->protocol,
         fcl::plugins::p2p_node::remote_options{.open_deadline = impl_->open_deadline(options),
                                                .deadline = impl_->query_deadline(options)});
      auto result = co_await remote->query(query{});
      validate_response(result.apis, impl_->settings);
      auto entries = std::move(result.apis);
      impl_->store_peer(peer, entries);
      co_return entries;
   }

   boost::asio::awaitable<resolution> resolve(fcl::p2p::peer_id peer, fcl::api::api_ref api,
                                              resolve_options options) override {
      const auto entries = co_await peer_apis(std::move(peer), options);
      if (auto selected = select_compatible(entries, api)) {
         co_return resolution{.api = std::move(*selected)};
      }
      const auto has_same_api = std::ranges::any_of(entries, [&](const auto& candidate) {
         return candidate.id == api.id && candidate.version.major == api.major;
      });
      if (has_same_api) {
         FCL_THROW_EXCEPTION(exceptions::incompatible_api, "remote peer has incompatible API revision",
                             fcl::exceptions::ctx("api", api.id.value));
      }
      FCL_THROW_EXCEPTION(exceptions::not_found, "remote peer does not advertise requested API",
                          fcl::exceptions::ctx("api", api.id.value));
   }

   boost::asio::awaitable<resolved_connection>
   open_resolved_connection(fcl::p2p::peer_id peer, fcl::api::api_ref api, fcl::api::descriptor descriptor,
                            resolve_options options) override {
      auto selected = co_await resolve(peer, api, options);
      validate_descriptor_compatible(descriptor, selected.api);
      auto protocol = fcl::p2p::protocol_id{.value = selected.api.protocol};
      auto connection = co_await impl_->p2p->open_api_connection(
         std::move(peer), std::move(protocol),
         fcl::plugins::p2p_node::remote_options{
            .open_deadline = impl_->open_deadline(options),
            .codec = selected.api.codec,
            .max_inflight = static_cast<std::size_t>(selected.api.max_inflight),
            .max_frame_size = static_cast<std::uint32_t>(selected.api.max_frame_size),
         });
      co_return resolved_connection{
         .connection = std::move(connection),
         .selected = fcl::api::api_ref{
            .id = std::move(selected.api.id),
            .major = selected.api.version.major,
            .min_revision = selected.api.version.revision,
         },
      };
   }

 private:
   std::shared_ptr<plugin::impl> impl_;
};

plugin::plugin() : impl_{std::make_shared<impl>()} {}
plugin::~plugin() = default;

fcl::p2p::protocol_id default_protocol() {
   return fcl::p2p::protocol_id{.value = "/fcl/api/resolver/1"};
}

fcl::app::plugin_id plugin::id() const {
   return fcl::app::plugin_id{.value = "fcl.p2p_api_resolver"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> plugin::describe_config() const {
   return fcl::config::describe_component<config>("p2p-api-resolver");
}

boost::asio::awaitable<void> plugin::configure(fcl::config::component_view view) {
   auto config = decode_config(view);
   validate_config(config);
   impl_->settings = std::move(config);
   impl_->protocol = fcl::p2p::protocol_id{.value = impl_->settings.protocol_id};
   co_return;
}

boost::asio::awaitable<void> plugin::provide(fcl::api::provider& provider) {
   provider.install<api>(std::make_shared<api_impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(fcl::app::plugin_context& context) {
   impl_->p2p = context.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0}).operator->();
   impl_->protocol_registry.clear();
   impl_->protocol_registry.install<detail::resolver_protocol>(std::make_shared<protocol_impl>(impl_));
   auto plan = fcl::api::binding()
                  .serve(impl_->protocol_registry)
                  .export_api<detail::resolver_protocol>({.id = {resolver_api_id}, .major = 1, .min_revision = 0})
                  .build();
   try {
      impl_->p2p->publish_api(std::move(plan), impl_->protocol, impl_->resolver_transport);
   } catch (const fcl::plugins::p2p_node::exceptions::route_conflict& error) {
      FCL_THROW_EXCEPTION(exceptions::duplicate_api, "P2P API resolver protocol conflicts with an existing route",
                          fcl::exceptions::ctx("protocol", impl_->protocol.value),
                          fcl::exceptions::ctx("error", error.message()));
   }
   impl_->initialized = true;
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   co_return;
}

void plugin::request_stop() noexcept {
   impl_->stopping = true;
}

boost::asio::awaitable<void> plugin::shutdown() {
   impl_->stopping = true;
   impl_->initialized = false;
   impl_->p2p = nullptr;
   auto lock = std::scoped_lock{impl_->mutex};
   impl_->cache.clear();
   co_return;
}

fcl::app::plugin_descriptor descriptor() {
   return fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "fcl.p2p_api_resolver"},
      .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_node"}},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace fcl::plugins::p2p_api_resolver
