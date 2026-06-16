module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_api_resolver.plugin;

import fcl.api.binding;
import fcl.api.registry;
import fcl.api.transport.options;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.p2p.protocol;
import fcl.plugins.p2p_api_resolver.api;
import fcl.plugins.p2p_api_resolver.exceptions;
import fcl.plugins.p2p_api_resolver.types;
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_node.exceptions;

#include "details/config.hxx"
#include "details/descriptor_projection.hxx"
#include "details/plugin_impl.hxx"
#include "details/resolver_api.hxx"

namespace fcl::plugins::p2p_api_resolver {

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
   provider.install<api>(std::make_shared<resolver_api>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(fcl::app::plugin_context& context) {
   impl_->p2p = context.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0}).operator->();
   try {
      impl_->install_protocol();
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
