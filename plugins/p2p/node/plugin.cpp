module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
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
import forge.app.views;
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
namespace {

[[nodiscard]] std::string reachability_name(forge::p2p::reachability::state value) {
   switch (value) {
   case forge::p2p::reachability::state::unknown:
      return "unknown";
   case forge::p2p::reachability::state::publicly_reachable:
      return "public";
   case forge::p2p::reachability::state::private_network:
      return "private";
   case forge::p2p::reachability::state::blocked:
      return "blocked";
   case forge::p2p::reachability::state::relay_only:
      return "relay-only";
   }
   return "unknown";
}

[[nodiscard]] std::uint64_t parse_cursor(std::string_view cursor) {
   if (cursor.empty()) {
      return 0;
   }
   auto out = std::uint64_t{0};
   const auto* first = cursor.data();
   const auto* last = cursor.data() + cursor.size();
   const auto result = std::from_chars(first, last, out);
   if (result.ec != std::errc{} || result.ptr != last) {
      return 0;
   }
   return out;
}

[[nodiscard]] std::uint64_t saturated_add(std::uint64_t left, std::uint64_t right) {
   const auto max = (std::numeric_limits<std::uint64_t>::max)();
   if (left > max - right) {
      return max;
   }
   return left + right;
}

[[nodiscard]] std::size_t to_size(std::uint64_t value) {
   const auto max = static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)());
   return static_cast<std::size_t>(std::min(value, max));
}

class peers_view_source final : public forge::app::view_source {
 public:
   using snapshot_reader = std::function<forge::p2p::diagnostics::snapshot(forge::p2p::diagnostics::options)>;

   explicit peers_view_source(snapshot_reader reader) : reader_{std::move(reader)} {}

   boost::asio::awaitable<forge::app::view_snapshot> snapshot(forge::app::view_query query) override {
      const auto cursor = parse_cursor(query.cursor);
      const auto limit = query.limit == 0 ? 100 : query.limit;
      const auto requested_end = saturated_add(cursor, limit);
      const auto probe_end = saturated_add(requested_end, 1);

      auto options = forge::p2p::diagnostics::options{};
      options.max_peers = to_size(probe_end);

      const auto diagnostics = reader_(options);
      const auto begin = to_size(cursor);
      const auto end = to_size(requested_end);
      const auto& peers = diagnostics.peers;

      auto out = forge::app::view_snapshot{
         .descriptor =
            {
               .id = "forge.plugins.p2p.node.peers",
               .title = "P2P Peers",
               .category = "network",
               .kind = forge::app::view_kind::table,
            },
         .columns =
            {
               {.id = "peer", .title = "Peer"},
               {.id = "reachability", .title = "Reachability"},
               {.id = "endpoints", .title = "Endpoints"},
               {.id = "protocols", .title = "Protocols"},
               {.id = "score", .title = "Score"},
               {.id = "protected", .title = "Protected"},
            },
      };

      if (begin < peers.size()) {
         const auto row_end = std::min(end, peers.size());
         out.page.rows.reserve(row_end - begin);
         for (auto index = begin; index < row_end; ++index) {
            const auto& peer = peers[index];
            out.page.rows.push_back(forge::app::view_table_row{
               .cells =
                  {
                     peer.peer.to_string(),
                     reachability_name(peer.reachability),
                     std::to_string(peer.endpoints.size()),
                     std::to_string(peer.protocols.size()),
                     std::to_string(peer.score),
                     peer.protected_peer ? "yes" : "no",
                  },
               .severity = peer.failures > peer.successes ? forge::app::view_severity::warning
                                                          : forge::app::view_severity::info,
            });
         }
      }

      if (peers.size() > end) {
         out.page.next_cursor = std::to_string(end);
      } else {
         out.page.total_estimate = peers.size();
      }
      co_return out;
   }

 private:
   snapshot_reader reader_;
};

} // namespace

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
   impl_->views = &context.views();
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
   if (impl_->views != nullptr) {
      impl_->peers_view = impl_->views->register_source(
         forge::app::view_descriptor{
            .id = "forge.plugins.p2p.node.peers",
            .title = "P2P Peers",
            .category = "network",
            .kind = forge::app::view_kind::table,
         },
         std::make_shared<peers_view_source>([state = impl_](forge::p2p::diagnostics::options options) {
            return state->require_node().diagnostics(options);
         }));
   }
}

void plugin::request_stop() noexcept {
   impl_->stopping = true;
   if (impl_->raw) {
      impl_->raw->stop();
   }
}

boost::asio::awaitable<void> plugin::shutdown() {
   impl_->peers_view.unregister();
   request_stop();
   if (impl_->node) {
      co_await impl_->node->async_stop();
      impl_->node.reset();
      impl_->raw = nullptr;
   }
   impl_->views = nullptr;
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
