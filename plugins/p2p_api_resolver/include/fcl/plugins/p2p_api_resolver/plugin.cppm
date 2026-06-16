module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string>

export module fcl.plugins.p2p_api_resolver.plugin;

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
import fcl.app.plugin_registry;
import fcl.config.component;
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

export namespace fcl::plugins::p2p_api_resolver {

class plugin final : public fcl::app::plugin {
 public:
   plugin();
   ~plugin() override;

   plugin(const plugin&) = delete;
   plugin& operator=(const plugin&) = delete;

   [[nodiscard]] fcl::app::plugin_id id() const override;
   [[nodiscard]] std::string version() const override;
   [[nodiscard]] std::optional<fcl::config::component_descriptor> describe_config() const override;
   boost::asio::awaitable<void> configure(fcl::config::component_view view) override;
   boost::asio::awaitable<void> provide(fcl::api::provider& provider) override;
   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override;
   boost::asio::awaitable<void> startup() override;
   void request_stop() noexcept override;
   boost::asio::awaitable<void> shutdown() override;

 private:
   struct impl;
   class resolver_protocol_service;
   class resolver_api;
   std::shared_ptr<impl> impl_;
};

[[nodiscard]] fcl::app::plugin_descriptor descriptor();
[[nodiscard]] fcl::p2p::protocol_id default_protocol();

} // namespace fcl::plugins::p2p_api_resolver
