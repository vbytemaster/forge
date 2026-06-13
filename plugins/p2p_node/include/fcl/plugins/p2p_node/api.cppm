module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/api_macros.hpp>

#include <optional>
#include <utility>
#include <vector>

export module fcl.plugins.p2p_node.api;

import fcl.api;
import fcl.api.transport;
import fcl.p2p;
import fcl.plugins.p2p_node.types;

export namespace fcl::plugins::p2p_node {

class api : public fcl::api::contract<api> {
 public:
   virtual ~api() = default;

   [[nodiscard]] virtual fcl::p2p::peer_id local_peer() const = 0;
   [[nodiscard]] virtual std::optional<fcl::p2p::endpoint> local_endpoint() const = 0;
   [[nodiscard]] virtual std::vector<fcl::p2p::endpoint> local_endpoints() const = 0;
   [[nodiscard]] virtual info network_info() const = 0;

   virtual void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) = 0;
   virtual void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol,
                            fcl::api::transport::options options) = 0;
   virtual void publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::node::protocol_handler handler) = 0;

   virtual boost::asio::awaitable<fcl::api::transport::connection>
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

} // namespace fcl::plugins::p2p_node

export {
FCL_API(::fcl::plugins::p2p_node::api, FCL_API_CONTRACT("fcl.plugins.p2p_node", 1, 0))
FCL_API(::fcl::plugins::p2p_node::diagnostics_source,
        FCL_API_CONTRACT("fcl.plugins.p2p_node.diagnostics_source", 1, 0))
FCL_API(::fcl::plugins::p2p_node::pubsub_source,
        FCL_API_CONTRACT("fcl.plugins.p2p_node.pubsub_source", 1, 0))
}
