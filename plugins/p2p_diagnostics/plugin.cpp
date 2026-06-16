module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_diagnostics.plugin;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
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
import fcl.plugins.p2p_node.plugin;
import fcl.plugins.p2p_diagnostics.api;
import fcl.plugins.p2p_diagnostics.exceptions;
import fcl.plugins.p2p_diagnostics.types;

#include "details/config.hxx"
#include "details/state.hxx"
#include "details/api_facade.hxx"

namespace fcl::plugins::p2p_diagnostics {

plugin::plugin() : impl_{std::make_shared<impl>()} {}
plugin::~plugin() = default;

fcl::app::plugin_id plugin::id() const {
   return fcl::app::plugin_id{.value = "fcl.p2p_diagnostics"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> plugin::describe_config() const {
   return fcl::config::describe_component<config>("p2p-diagnostics");
}

boost::asio::awaitable<void> plugin::configure(fcl::config::component_view view) {
   auto config = decode_config(view);
   validate_config(config);
   impl_->settings = std::move(config);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(fcl::api::provider& provider) {
   provider.install<api>(std::make_shared<api_impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(fcl::app::plugin_context& context) {
   impl_->source = context.apis()
                      .get<fcl::plugins::p2p_node::diagnostics_source>(
                         {.id = {"fcl.plugins.p2p_node.diagnostics_source"}, .major = 1, .min_revision = 0})
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

fcl::app::plugin_descriptor descriptor() {
   return fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "fcl.p2p_diagnostics"},
      .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_node"}},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace fcl::plugins::p2p_diagnostics
