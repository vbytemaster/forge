module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p.node.plugin;

import fcl.transport.api.options;
import fcl.asio.runtime;
import fcl.p2p.endpoint;
import fcl.p2p.identity;
import fcl.p2p.node;
import fcl.p2p.protocol;
import fcl.p2p.pubsub;
import fcl.p2p.scoring;
import fcl.plugins.p2p.node.api;
import fcl.plugins.p2p.node.exceptions;
import fcl.plugins.p2p.node.types;

#include "details/plugin_impl.hxx"
#include "details/pubsub_source.hxx"

namespace fcl::plugins::p2p::node {

plugin::pubsub_source_adapter::pubsub_source_adapter(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

void plugin::pubsub_source_adapter::enable(fcl::p2p::pubsub::options options) {
   if (impl_->started || impl_->node) {
      FCL_THROW_EXCEPTION(exceptions::route_conflict,
                          "P2P PubSub capability must be requested before startup");
   }
   impl_->pubsub_options = std::move(options);
   impl_->pubsub_requested = true;
}

fcl::p2p::peer_id plugin::pubsub_source_adapter::local_peer() const {
   return impl_->require_node().local_peer();
}

boost::asio::awaitable<fcl::p2p::pubsub::message>
plugin::pubsub_source_adapter::async_publish_message(fcl::p2p::pubsub::topic subject,
                                                  std::vector<std::uint8_t> data,
                                                  fcl::p2p::pubsub::publish_options options) {
   co_return co_await impl_->require_node().async_publish(std::move(subject), std::move(data), options);
}

boost::asio::awaitable<fcl::p2p::pubsub::subscription>
plugin::pubsub_source_adapter::async_join_topic(fcl::p2p::pubsub::topic subject,
                                             fcl::p2p::pubsub::handler handler) {
   co_return co_await impl_->require_node().async_subscribe(std::move(subject), std::move(handler));
}

boost::asio::awaitable<void> plugin::pubsub_source_adapter::async_leave_topic(fcl::p2p::pubsub::topic subject) {
   co_await impl_->require_node().async_unsubscribe(std::move(subject));
}

fcl::p2p::pubsub::snapshot plugin::pubsub_source_adapter::snapshot() const {
   return impl_->require_node().pubsub_snapshot();
}

} // namespace fcl::plugins::p2p::node
