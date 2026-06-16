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

import fcl.api.transport.options;
import fcl.asio.runtime;
import fcl.p2p.diagnostics;
import fcl.p2p.endpoint;
import fcl.p2p.identity;
import fcl.p2p.node;
import fcl.p2p.protocol;
import fcl.p2p.pubsub;
import fcl.p2p.scoring;
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_node.types;

#include "details/plugin_impl.hxx"
#include "details/diagnostics_source.hxx"

namespace fcl::plugins::p2p_node {

plugin::diagnostics_source_adapter::diagnostics_source_adapter(std::shared_ptr<plugin::impl> impl)
    : impl_{std::move(impl)} {}

fcl::p2p::diagnostics::snapshot
plugin::diagnostics_source_adapter::snapshot(fcl::p2p::diagnostics::options options) const {
   return impl_->require_node().diagnostics(options);
}

} // namespace fcl::plugins::p2p_node
