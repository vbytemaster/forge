module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_node.plugin;

import fcl.api.binding;
import fcl.api.transport.connection;
import fcl.api.transport.options;
import fcl.asio.runtime;
import fcl.p2p.api;
import fcl.p2p.endpoint;
import fcl.p2p.identity;
import fcl.p2p.node;
import fcl.p2p.protocol;
import fcl.p2p.pubsub;
import fcl.p2p.scoring;
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_node.types;

#include "details/plugin_impl.hxx"
#include "details/node_api.hxx"

namespace fcl::plugins::p2p_node {

plugin::node_api::node_api(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

fcl::p2p::peer_id plugin::node_api::local_peer() const {
   return impl_->require_node().local_peer();
}

std::optional<fcl::p2p::endpoint> plugin::node_api::local_endpoint() const {
   return impl_->require_node().local_endpoint();
}

std::vector<fcl::p2p::endpoint> plugin::node_api::local_endpoints() const {
   return impl_->require_node().local_endpoints();
}

info plugin::node_api::network_info() const {
   return info{
      .local_peer = impl_->require_node().local_peer(),
      .local_endpoints = impl_->require_node().local_endpoints(),
      .started = impl_->started,
   };
}

void plugin::node_api::publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) {
   publish_api(std::move(plan), std::move(protocol), impl_->api_options);
}

void plugin::node_api::publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol,
                                   fcl::api::transport::options options) {
   auto binding = fcl::p2p::api()
                     .use(std::move(plan))
                     .protocol_id(protocol)
                     .codec(options.codec)
                     .max_inflight_per_peer(options.max_inflight)
                     .deadline(options.deadline)
                     .max_frame_size(options.max_frame_size)
                     .build();
   impl_->add_route(binding.protocol(), binding.handler());
}

void plugin::node_api::publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::node::protocol_handler handler) {
   auto binding = fcl::p2p::route().protocol_id(std::move(protocol)).handler(std::move(handler)).build();
   impl_->add_route(binding.protocol(), binding.handler());
}

boost::asio::awaitable<fcl::api::transport::connection>
plugin::node_api::open_api_connection(fcl::p2p::peer_id peer,
                                      fcl::p2p::protocol_id protocol,
                                      remote_options options) {
   auto stream = co_await impl_->require_node().async_open_protocol_stream(std::move(peer), std::move(protocol),
                                                                            impl_->open_options_for(options));
   co_return fcl::api::transport::connection{std::move(stream).into_transport_stream(), impl_->api_options_for(options)};
}

} // namespace fcl::plugins::p2p_node
