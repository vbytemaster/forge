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

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.p2p;
import fcl.plugins.p2p_node;
import fcl.plugins.p2p_diagnostics.api;
import fcl.plugins.p2p_diagnostics.exceptions;
import fcl.plugins.p2p_diagnostics.types;

namespace fcl::plugins::p2p_diagnostics {
namespace {

[[nodiscard]] config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      auto message = std::string{"invalid P2P diagnostics config"};
      if (!decoded.diagnostics.entries.empty()) {
         const auto& first = decoded.diagnostics.entries.front();
         message += ": " + first.path + " " + first.code + " " + first.message;
      }
      FCL_THROW_EXCEPTION(exceptions::invalid_config, message);
   }
   return std::move(decoded.value);
}

void validate_config(const config& value) {
   if (value.max_peers == 0 || value.max_sessions == 0 || value.max_endpoints_per_peer == 0 ||
       value.max_protocols_per_peer == 0 || value.max_relay_reservations_per_peer == 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "P2P diagnostics limits must be positive");
   }
}

[[nodiscard]] fcl::p2p::diagnostics::options configured_options(const config& value) {
   return fcl::p2p::diagnostics::options{
      .max_peers = static_cast<std::size_t>(value.max_peers),
      .max_sessions = static_cast<std::size_t>(value.max_sessions),
      .max_endpoints_per_peer = static_cast<std::size_t>(value.max_endpoints_per_peer),
      .max_protocols_per_peer = static_cast<std::size_t>(value.max_protocols_per_peer),
      .max_relay_reservations_per_peer = static_cast<std::size_t>(value.max_relay_reservations_per_peer),
   };
}

[[nodiscard]] std::vector<fcl::p2p::diagnostics::peer>
filter_peers(const fcl::p2p::diagnostics::snapshot& snapshot, const filter& filter) {
   auto connected = std::set<fcl::p2p::peer_id>{};
   if (filter.only_connected) {
      for (const auto& session : snapshot.sessions) {
         connected.insert(session.remote_peer);
      }
   }

   auto out = std::vector<fcl::p2p::diagnostics::peer>{};
   out.reserve(snapshot.peers.size());
   for (const auto& peer : snapshot.peers) {
      if (filter.peer.has_value() && peer.peer != *filter.peer) {
         continue;
      }
      if (filter.only_connected && !connected.contains(peer.peer)) {
         continue;
      }
      if (filter.only_protected && !peer.protected_peer) {
         continue;
      }
      out.push_back(peer);
      if (filter.limit != 0 && out.size() >= filter.limit) {
         break;
      }
   }
   return out;
}

} // namespace

struct plugin::impl : public std::enable_shared_from_this<plugin::impl> {
   config settings;
   std::shared_ptr<fcl::plugins::p2p_node::diagnostics_source> source;
   bool initialized = false;
   bool stopping = false;

   [[nodiscard]] fcl::plugins::p2p_node::diagnostics_source& require_source() const {
      if (!initialized || !source) {
         FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P diagnostics plugin is not initialized");
      }
      return *source;
   }

   [[nodiscard]] fcl::p2p::diagnostics::snapshot snapshot() const {
      return require_source().snapshot(configured_options(settings));
   }
};

class plugin::api_impl final : public api {
 public:
   explicit api_impl(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

   fcl::p2p::diagnostics::snapshot snapshot() const override {
      return impl_->snapshot();
   }

   fcl::p2p::diagnostics::snapshot snapshot(fcl::p2p::diagnostics::options options) const override {
      return impl_->require_source().snapshot(options);
   }

   fcl::p2p::diagnostics::network_state network() const override {
      return impl_->snapshot().network;
   }

   fcl::p2p::resource_manager::snapshot resources() const override {
      return impl_->snapshot().resources;
   }

   fcl::p2p::pubsub::snapshot pubsub() const override {
      return impl_->snapshot().pubsub;
   }

   std::vector<fcl::p2p::diagnostics::peer> peers(filter value) const override {
      return filter_peers(impl_->snapshot(), value);
   }

   fcl::p2p::diagnostics::peer peer(fcl::p2p::peer_id value) const override {
      auto values = peers(filter{.peer = std::move(value), .limit = 1});
      if (values.empty()) {
         FCL_THROW_EXCEPTION(exceptions::not_found, "P2P diagnostics peer was not found");
      }
      return std::move(values.front());
   }

 private:
   std::shared_ptr<plugin::impl> impl_;
};

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
