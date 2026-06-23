module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.p2p.resolver.plugin;

import forge.api.binding;
import forge.api.registry;
import forge.transport.api.options;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.config.component;
import forge.config.decode;
import forge.exceptions;
import forge.p2p.identity;
import forge.p2p.protocol;
import forge.plugins.p2p.resolver.api;
import forge.plugins.p2p.resolver.exceptions;
import forge.plugins.p2p.resolver.types;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.exceptions;

#include "details/config.hxx"
#include "details/descriptor_projection.hxx"
#include "details/plugin_impl.hxx"
#include "details/resolver_api.hxx"

namespace forge::plugins::p2p::resolver {

plugin::plugin() : impl_{std::make_shared<impl>()} {}
plugin::~plugin() = default;

forge::p2p::protocol_id default_protocol() {
   return forge::p2p::protocol_id{.value = "/forge/api/resolver/1"};
}

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.p2p.resolver"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<forge::config::component_descriptor> plugin::describe_config() const {
   return forge::config::describe_component<config>("plugins.p2p.resolver");
}

boost::asio::awaitable<void> plugin::configure(forge::config::component_view view) {
   auto config = decode_config(view);
   validate_config(config);
   impl_->settings = std::move(config);
   impl_->protocol = forge::p2p::protocol_id{.value = impl_->settings.protocol_id};
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::provider& provider) {
   provider.install<api>(std::make_shared<resolver_api>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context& context) {
   impl_->p2p = context.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0}).operator->();
   try {
      impl_->install_protocol();
   } catch (const forge::plugins::p2p::node::exceptions::route_conflict& error) {
      FORGE_THROW_EXCEPTION(exceptions::duplicate_api, "P2P API resolver protocol conflicts with an existing route",
                          forge::exceptions::ctx("protocol", impl_->protocol.value),
                          forge::exceptions::ctx("error", error.message()));
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

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "forge.plugins.p2p.resolver"},
      .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.node"}},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace forge::plugins::p2p::resolver
