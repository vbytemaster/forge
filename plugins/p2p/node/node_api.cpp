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

module forge.plugins.p2p.node.plugin;

import forge.api.binding;
import forge.transport.api.connection;
import forge.transport.api.options;
import forge.app.views;
import forge.asio.runtime;
import forge.p2p.api;
import forge.p2p.endpoint;
import forge.p2p.identity;
import forge.p2p.node;
import forge.p2p.protocol;
import forge.p2p.pubsub;
import forge.p2p.scoring;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.types;

#include "details/plugin_impl.hxx"
#include "details/node_api.hxx"

namespace forge::plugins::p2p::node {

plugin::node_api::node_api(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

forge::p2p::peer_id plugin::node_api::local_peer() const {
   return impl_->require_node().local_peer();
}

std::optional<forge::p2p::endpoint> plugin::node_api::local_endpoint() const {
   return impl_->require_node().local_endpoint();
}

std::vector<forge::p2p::endpoint> plugin::node_api::local_endpoints() const {
   return impl_->require_node().local_endpoints();
}

info plugin::node_api::network_info() const {
   return info{
      .local_peer = impl_->require_node().local_peer(),
      .local_endpoints = impl_->require_node().local_endpoints(),
      .started = impl_->started,
   };
}

void plugin::node_api::publish_api(forge::api::binding_plan plan, forge::p2p::protocol_id protocol) {
   publish_api(std::move(plan), std::move(protocol), impl_->api_options);
}

void plugin::node_api::publish_api(forge::api::binding_plan plan, forge::p2p::protocol_id protocol,
                                   forge::transport::api::options options) {
   auto binding = forge::p2p::api()
                     .use(std::move(plan))
                     .protocol_id(protocol)
                     .codec(options.codec)
                     .max_inflight_per_peer(options.max_inflight)
                     .deadline(options.deadline)
                     .max_frame_size(options.max_frame_size)
                     .build();
   impl_->add_route(binding.protocol(), binding.handler());
}

void plugin::node_api::publish_protocol(forge::p2p::protocol_id protocol, forge::p2p::node::protocol_handler handler) {
   auto binding = forge::p2p::route().protocol_id(std::move(protocol)).handler(std::move(handler)).build();
   impl_->add_route(binding.protocol(), binding.handler());
}

boost::asio::awaitable<forge::transport::api::connection>
plugin::node_api::open_api_connection(forge::p2p::peer_id peer,
                                      forge::p2p::protocol_id protocol,
                                      remote_options options) {
   auto stream = co_await impl_->require_node().async_open_protocol_stream(std::move(peer), std::move(protocol),
                                                                            impl_->open_options_for(options));
   co_return forge::transport::api::connection{std::move(stream).into_transport_stream(), impl_->api_options_for(options)};
}

} // namespace forge::plugins::p2p::node
