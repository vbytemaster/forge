module;

#include <fcl/exceptions/macros.hpp>

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

module fcl.plugins.p2p_node.plugin;

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
import fcl.asio.runtime;
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
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_node.exceptions;
import fcl.plugins.p2p_node.types;

#include "details/config.hxx"
#include "details/diagnostics_source.hxx"
#include "details/node_api.hxx"
#include "details/plugin_impl.hxx"
#include "details/pubsub_source.hxx"

namespace fcl::plugins::p2p_node {

plugin::plugin() : impl_{std::make_shared<impl>()} {}
plugin::~plugin() = default;

fcl::app::plugin_id plugin::id() const {
   return fcl::app::plugin_id{.value = "fcl.p2p_node"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> plugin::describe_config() const {
   return fcl::config::describe_component<config>("p2p");
}

boost::asio::awaitable<void> plugin::configure(fcl::config::component_view view) {
   const auto config = decode_config(view);
   apply_config(*impl_, config);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(fcl::api::provider& provider) {
   provider.install<api>(std::make_shared<node_api>(impl_));
   provider.install<diagnostics_source>(std::make_shared<diagnostics_source_adapter>(impl_));
   provider.install<pubsub_source>(std::make_shared<pubsub_source_adapter>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(fcl::app::plugin_context& context) {
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
         fcl::exceptions::capture_and_log("P2P bootstrap connect failed");
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

fcl::app::plugin_descriptor descriptor() {
   return fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "fcl.p2p_node"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace fcl::plugins::p2p_node
