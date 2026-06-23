module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.p2p.diagnostics.plugin;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.config.component;
import forge.config.decode;
import forge.exceptions;
import forge.p2p.exceptions;
import forge.p2p.identity;
import forge.p2p.endpoint;
import forge.p2p.envelope;
import forge.p2p.identify;
import forge.p2p.diagnostics;
import forge.p2p.discovery;
import forge.p2p.dht;
import forge.p2p.rendezvous;
import forge.p2p.pubsub;
import forge.p2p.reachability;
import forge.p2p.hole_punch;
import forge.p2p.protocol;
import forge.p2p.message;
import forge.p2p.scoring;
import forge.p2p.relay;
import forge.p2p.resource_manager;
import forge.p2p.stream;
import forge.p2p.negotiation;
import forge.p2p.peer_store;
import forge.p2p.node;
import forge.p2p.api;
import forge.plugins.p2p.node.types;
import forge.plugins.p2p.node.exceptions;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.diagnostics.api;
import forge.plugins.p2p.diagnostics.exceptions;
import forge.plugins.p2p.diagnostics.types;

#include "details/config.hxx"
#include "details/diagnostics_api.hxx"
#include "details/plugin_impl.hxx"

namespace forge::plugins::p2p::diagnostics {

plugin::plugin() : impl_{std::make_shared<impl>()} {}
plugin::~plugin() = default;

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.p2p.diagnostics"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<forge::config::component_descriptor> plugin::describe_config() const {
   return forge::config::describe_component<config>("plugins.p2p.diagnostics");
}

boost::asio::awaitable<void> plugin::configure(forge::config::component_view view) {
   auto config = decode_config(view);
   validate_config(config);
   impl_->settings = std::move(config);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::provider& provider) {
   provider.install<api>(std::make_shared<diagnostics_api>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context& context) {
   impl_->source = context.apis()
                      .get<forge::plugins::p2p::node::diagnostics_source>(
                         {.id = {"forge.plugins.p2p.node.diagnostics_source"}, .major = 1, .min_revision = 0})
                      .shared();
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
   impl_->source = nullptr;
   co_return;
}

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "forge.plugins.p2p.diagnostics"},
      .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.node"}},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace forge::plugins::p2p::diagnostics
