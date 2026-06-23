module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.plugins.p2p.node.plugin;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.transport.api.exceptions;
import forge.transport.api.options;
import forge.transport.api.client;
import forge.transport.api.connection;
import forge.transport.api.server;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.asio.runtime;
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
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.exceptions;
import forge.plugins.p2p.node.types;

#include "details/config.hxx"
#include "details/diagnostics_source.hxx"
#include "details/node_api.hxx"
#include "details/plugin_impl.hxx"
#include "details/pubsub_source.hxx"

namespace forge::plugins::p2p::node {

plugin::plugin() : impl_{std::make_shared<impl>()} {}
plugin::~plugin() = default;

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.p2p.node"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<forge::config::component_descriptor> plugin::describe_config() const {
   return forge::config::describe_component<config>("plugins.p2p.node");
}

boost::asio::awaitable<void> plugin::configure(forge::config::component_view view) {
   const auto config = decode_config(view);
   apply_config(*impl_, config);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::provider& provider) {
   provider.install<api>(std::make_shared<node_api>(impl_));
   provider.install<diagnostics_source>(std::make_shared<diagnostics_source_adapter>(impl_));
   provider.install<pubsub_source>(std::make_shared<pubsub_source_adapter>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context& context) {
   impl_->runtime = &context.scheduler().runtime_context();
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   auto& node = impl_->ensure_node();
   for (auto& route : impl_->routes) {
      node.register_protocol_handler(route.first, route.second);
   }
   for (const auto& endpoint : impl_->listen) {
      co_await node.async_listen(endpoint);
   }
   for (const auto& endpoint : impl_->bootstrap) {
      try {
         (void)co_await node.async_connect(endpoint);
      } catch (...) {
         forge::exceptions::capture_and_log("P2P bootstrap connect failed");
      }
   }
   impl_->started = true;
}

void plugin::request_stop() noexcept {
   impl_->stopping = true;
   if (impl_->raw) {
      impl_->raw->stop();
   }
}

boost::asio::awaitable<void> plugin::shutdown() {
   request_stop();
   if (impl_->node) {
      co_await impl_->node->async_stop();
      impl_->node.reset();
      impl_->raw = nullptr;
   }
   impl_->started = false;
}

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "forge.plugins.p2p.node"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace forge::plugins::p2p::node
