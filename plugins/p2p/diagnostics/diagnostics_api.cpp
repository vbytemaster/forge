module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <utility>
#include <vector>

module forge.plugins.p2p.diagnostics.plugin;

import forge.p2p.diagnostics;
import forge.p2p.identity;
import forge.p2p.pubsub;
import forge.p2p.resource_manager;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.diagnostics.api;
import forge.plugins.p2p.diagnostics.exceptions;
import forge.plugins.p2p.diagnostics.types;

#include "details/config.hxx"
#include "details/diagnostics_api.hxx"
#include "details/plugin_impl.hxx"

namespace forge::plugins::p2p::diagnostics {

plugin::diagnostics_api::diagnostics_api(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

forge::p2p::diagnostics::snapshot plugin::diagnostics_api::snapshot() const {
   return impl_->snapshot();
}

forge::p2p::diagnostics::snapshot plugin::diagnostics_api::snapshot(forge::p2p::diagnostics::options options) const {
   return impl_->require_source().snapshot(options);
}

forge::p2p::diagnostics::network_state plugin::diagnostics_api::network() const {
   return impl_->snapshot().network;
}

forge::p2p::resource_manager::snapshot plugin::diagnostics_api::resources() const {
   return impl_->snapshot().resources;
}

forge::p2p::pubsub::snapshot plugin::diagnostics_api::pubsub() const {
   return impl_->snapshot().pubsub;
}

std::vector<forge::p2p::diagnostics::peer> plugin::diagnostics_api::peers(filter value) const {
   return filter_peers(impl_->snapshot(), value);
}

forge::p2p::diagnostics::peer plugin::diagnostics_api::peer(forge::p2p::peer_id value) const {
   auto values = peers(filter{.peer = std::move(value), .limit = 1});
   if (values.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "P2P diagnostics peer was not found");
   }
   return std::move(values.front());
}

} // namespace forge::plugins::p2p::diagnostics
