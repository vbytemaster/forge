module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/macros.hpp>

#include <optional>
#include <utility>
#include <vector>

export module forge.plugins.p2p.node.api;

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
import forge.p2p.identity;
import forge.p2p.endpoint;
import forge.p2p.diagnostics;
import forge.p2p.pubsub;
import forge.p2p.protocol;
import forge.p2p.node;
import forge.plugins.p2p.node.types;

export namespace forge::plugins::p2p::node {

class api : public forge::api::contract<api> {
 public:
   virtual ~api() = default;

   [[nodiscard]] virtual forge::p2p::peer_id local_peer() const = 0;
   [[nodiscard]] virtual std::optional<forge::p2p::endpoint> local_endpoint() const = 0;
   [[nodiscard]] virtual std::vector<forge::p2p::endpoint> local_endpoints() const = 0;
   [[nodiscard]] virtual info network_info() const = 0;

   virtual void publish_api(forge::api::binding_plan plan, forge::p2p::protocol_id protocol) = 0;
   virtual void publish_api(forge::api::binding_plan plan, forge::p2p::protocol_id protocol,
                            forge::transport::api::options options) = 0;
   virtual void publish_protocol(forge::p2p::protocol_id protocol, forge::p2p::node::protocol_handler handler) = 0;

   virtual boost::asio::awaitable<forge::transport::api::connection>
   open_api_connection(forge::p2p::peer_id peer, forge::p2p::protocol_id protocol, remote_options options = {}) = 0;

   template <typename Interface>
   boost::asio::awaitable<forge::api::handle<Interface>>
   remote(forge::p2p::peer_id peer, forge::p2p::protocol_id protocol, remote_options options = {}) {
      auto connection = co_await open_api_connection(std::move(peer), std::move(protocol), options);
      co_return co_await connection.template get_remote_api<Interface>();
   }
};

class diagnostics_source : public forge::api::contract<diagnostics_source> {
 public:
   virtual ~diagnostics_source() = default;

   [[nodiscard]] virtual forge::p2p::diagnostics::snapshot
   snapshot(forge::p2p::diagnostics::options options = {}) const = 0;
};

class pubsub_source : public forge::api::contract<pubsub_source> {
 public:
   virtual ~pubsub_source() = default;

   virtual void enable(forge::p2p::pubsub::options options) = 0;
   [[nodiscard]] virtual forge::p2p::peer_id local_peer() const = 0;
   virtual boost::asio::awaitable<forge::p2p::pubsub::message>
   async_publish_message(forge::p2p::pubsub::topic subject, std::vector<std::uint8_t> data,
                         forge::p2p::pubsub::publish_options options) = 0;
   virtual boost::asio::awaitable<forge::p2p::pubsub::subscription>
   async_join_topic(forge::p2p::pubsub::topic subject, forge::p2p::pubsub::handler handler) = 0;
   virtual boost::asio::awaitable<void> async_leave_topic(forge::p2p::pubsub::topic subject) = 0;
   [[nodiscard]] virtual forge::p2p::pubsub::snapshot snapshot() const = 0;
};

} // namespace forge::plugins::p2p::node

export {
FORGE_API(::forge::plugins::p2p::node::api, FORGE_API_CONTRACT("forge.plugins.p2p.node", 1, 0))
FORGE_API(::forge::plugins::p2p::node::diagnostics_source,
        FORGE_API_CONTRACT("forge.plugins.p2p.node.diagnostics_source", 1, 0))
FORGE_API(::forge::plugins::p2p::node::pubsub_source,
        FORGE_API_CONTRACT("forge.plugins.p2p.node.pubsub_source", 1, 0))
}
