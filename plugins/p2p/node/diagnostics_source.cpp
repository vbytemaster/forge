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

import forge.transport.api.options;
import forge.asio.runtime;
import forge.p2p.diagnostics;
import forge.p2p.endpoint;
import forge.p2p.identity;
import forge.p2p.node;
import forge.p2p.protocol;
import forge.p2p.pubsub;
import forge.p2p.scoring;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.types;

#include "details/plugin_impl.hxx"
#include "details/diagnostics_source.hxx"

namespace forge::plugins::p2p::node {

plugin::diagnostics_source_adapter::diagnostics_source_adapter(std::shared_ptr<plugin::impl> impl)
    : impl_{std::move(impl)} {}

forge::p2p::diagnostics::snapshot
plugin::diagnostics_source_adapter::snapshot(forge::p2p::diagnostics::options options) const {
   return impl_->require_node().diagnostics(options);
}

} // namespace forge::plugins::p2p::node
