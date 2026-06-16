module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

module fcl.plugins.p2p_node.plugin;

import fcl.api.binding;
import fcl.api.transport.connection;
import fcl.api.transport.options;
import fcl.asio.runtime;
import fcl.exceptions;
import fcl.p2p.api;
import fcl.p2p.diagnostics;
import fcl.p2p.endpoint;
import fcl.p2p.identity;
import fcl.p2p.node;
import fcl.p2p.protocol;
import fcl.p2p.pubsub;
import fcl.p2p.scoring;
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_node.exceptions;
import fcl.plugins.p2p_node.types;

#include "details/state.hxx"
#include "details/api_facade.hxx"

namespace fcl::plugins::p2p_node {

plugin::api_impl::api_impl(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

fcl::p2p::peer_id plugin::api_impl::local_peer() const {
   return impl_->require_node().local_peer();
}

std::optional<fcl::p2p::endpoint> plugin::api_impl::local_endpoint() const {
   return impl_->require_node().local_endpoint();
}

std::vector<fcl::p2p::endpoint> plugin::api_impl::local_endpoints() const {
   return impl_->require_node().local_endpoints();
}

info plugin::api_impl::network_info() const {
   return info{
      .local_peer = impl_->require_node().local_peer(),
      .local_endpoints = impl_->require_node().local_endpoints(),
      .started = impl_->started,
   };
}

void plugin::api_impl::publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) {
   publish_api(std::move(plan), std::move(protocol), impl_->api_options);
}

void plugin::api_impl::publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol,
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

void plugin::api_impl::publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::node::protocol_handler handler) {
   auto binding = fcl::p2p::route().protocol_id(std::move(protocol)).handler(std::move(handler)).build();
   impl_->add_route(binding.protocol(), binding.handler());
}

boost::asio::awaitable<fcl::api::transport::connection>
plugin::api_impl::open_api_connection(fcl::p2p::peer_id peer,
                                      fcl::p2p::protocol_id protocol,
                                      remote_options options) {
   auto stream = co_await impl_->require_node().async_open_protocol_stream(std::move(peer), std::move(protocol),
                                                                            impl_->open_options_for(options));
   co_return fcl::api::transport::connection{std::move(stream).into_transport_stream(), impl_->api_options_for(options)};
}

plugin::diagnostics_source_impl::diagnostics_source_impl(std::shared_ptr<plugin::impl> impl)
    : impl_{std::move(impl)} {}

fcl::p2p::diagnostics::snapshot
plugin::diagnostics_source_impl::snapshot(fcl::p2p::diagnostics::options options) const {
   return impl_->require_node().diagnostics(options);
}

plugin::pubsub_source_impl::pubsub_source_impl(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

void plugin::pubsub_source_impl::enable(fcl::p2p::pubsub::options options) {
   if (impl_->started || impl_->node) {
      FCL_THROW_EXCEPTION(exceptions::route_conflict,
                          "P2P PubSub capability must be requested before startup");
   }
   impl_->pubsub_options = std::move(options);
   impl_->pubsub_requested = true;
}

fcl::p2p::peer_id plugin::pubsub_source_impl::local_peer() const {
   return impl_->require_node().local_peer();
}

boost::asio::awaitable<fcl::p2p::pubsub::message>
plugin::pubsub_source_impl::async_publish_message(fcl::p2p::pubsub::topic subject,
                                                  std::vector<std::uint8_t> data,
                                                  fcl::p2p::pubsub::publish_options options) {
   co_return co_await impl_->require_node().async_publish(std::move(subject), std::move(data), options);
}

boost::asio::awaitable<fcl::p2p::pubsub::subscription>
plugin::pubsub_source_impl::async_join_topic(fcl::p2p::pubsub::topic subject,
                                             fcl::p2p::pubsub::handler handler) {
   co_return co_await impl_->require_node().async_subscribe(std::move(subject), std::move(handler));
}

boost::asio::awaitable<void> plugin::pubsub_source_impl::async_leave_topic(fcl::p2p::pubsub::topic subject) {
   co_await impl_->require_node().async_unsubscribe(std::move(subject));
}

fcl::p2p::pubsub::snapshot plugin::pubsub_source_impl::snapshot() const {
   return impl_->require_node().pubsub_snapshot();
}

} // namespace fcl::plugins::p2p_node
