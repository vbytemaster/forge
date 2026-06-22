module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/macros.hpp>

#include <optional>
#include <utility>
#include <vector>

export module fcl.plugins.p2p.node.api;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.transport.api.exceptions;
import fcl.transport.api.options;
import fcl.transport.api.client;
import fcl.transport.api.connection;
import fcl.transport.api.server;
import fcl.p2p.identity;
import fcl.p2p.endpoint;
import fcl.p2p.diagnostics;
import fcl.p2p.pubsub;
import fcl.p2p.protocol;
import fcl.p2p.node;
import fcl.plugins.p2p.node.types;

export namespace fcl::plugins::p2p::node {

class api : public fcl::api::contract<api> {
 public:
   virtual ~api() = default;

   [[nodiscard]] virtual fcl::p2p::peer_id local_peer() const = 0;
   [[nodiscard]] virtual std::optional<fcl::p2p::endpoint> local_endpoint() const = 0;
   [[nodiscard]] virtual std::vector<fcl::p2p::endpoint> local_endpoints() const = 0;
   [[nodiscard]] virtual info network_info() const = 0;

   virtual void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) = 0;
   virtual void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol,
                            fcl::transport::api::options options) = 0;
   virtual void publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::node::protocol_handler handler) = 0;

   virtual boost::asio::awaitable<fcl::transport::api::connection>
   open_api_connection(fcl::p2p::peer_id peer, fcl::p2p::protocol_id protocol, remote_options options = {}) = 0;

   template <typename Interface>
   boost::asio::awaitable<fcl::api::handle<Interface>>
   remote(fcl::p2p::peer_id peer, fcl::p2p::protocol_id protocol, remote_options options = {}) {
      auto connection = co_await open_api_connection(std::move(peer), std::move(protocol), options);
      co_return co_await connection.template get_remote_api<Interface>();
   }
};

class diagnostics_source : public fcl::api::contract<diagnostics_source> {
 public:
   virtual ~diagnostics_source() = default;

   [[nodiscard]] virtual fcl::p2p::diagnostics::snapshot
   snapshot(fcl::p2p::diagnostics::options options = {}) const = 0;
};

class pubsub_source : public fcl::api::contract<pubsub_source> {
 public:
   virtual ~pubsub_source() = default;

   virtual void enable(fcl::p2p::pubsub::options options) = 0;
   [[nodiscard]] virtual fcl::p2p::peer_id local_peer() const = 0;
   virtual boost::asio::awaitable<fcl::p2p::pubsub::message>
   async_publish_message(fcl::p2p::pubsub::topic subject, std::vector<std::uint8_t> data,
                         fcl::p2p::pubsub::publish_options options) = 0;
   virtual boost::asio::awaitable<fcl::p2p::pubsub::subscription>
   async_join_topic(fcl::p2p::pubsub::topic subject, fcl::p2p::pubsub::handler handler) = 0;
   virtual boost::asio::awaitable<void> async_leave_topic(fcl::p2p::pubsub::topic subject) = 0;
   [[nodiscard]] virtual fcl::p2p::pubsub::snapshot snapshot() const = 0;
};

} // namespace fcl::plugins::p2p::node

export {
FCL_API(::fcl::plugins::p2p::node::api, FCL_API_CONTRACT("fcl.plugins.p2p.node", 1, 0))
FCL_API(::fcl::plugins::p2p::node::diagnostics_source,
        FCL_API_CONTRACT("fcl.plugins.p2p.node.diagnostics_source", 1, 0))
FCL_API(::fcl::plugins::p2p::node::pubsub_source,
        FCL_API_CONTRACT("fcl.plugins.p2p.node.pubsub_source", 1, 0))
}
