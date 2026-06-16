module;

#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <utility>
#include <vector>

module fcl.plugins.p2p_diagnostics.plugin;

import fcl.p2p.diagnostics;
import fcl.p2p.identity;
import fcl.p2p.pubsub;
import fcl.p2p.resource_manager;
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_diagnostics.api;
import fcl.plugins.p2p_diagnostics.exceptions;
import fcl.plugins.p2p_diagnostics.types;

#include "details/config.hxx"
#include "details/state.hxx"
#include "details/api_facade.hxx"

namespace fcl::plugins::p2p_diagnostics {

plugin::api_impl::api_impl(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

fcl::p2p::diagnostics::snapshot plugin::api_impl::snapshot() const {
   return impl_->snapshot();
}

fcl::p2p::diagnostics::snapshot plugin::api_impl::snapshot(fcl::p2p::diagnostics::options options) const {
   return impl_->require_source().snapshot(options);
}

fcl::p2p::diagnostics::network_state plugin::api_impl::network() const {
   return impl_->snapshot().network;
}

fcl::p2p::resource_manager::snapshot plugin::api_impl::resources() const {
   return impl_->snapshot().resources;
}

fcl::p2p::pubsub::snapshot plugin::api_impl::pubsub() const {
   return impl_->snapshot().pubsub;
}

std::vector<fcl::p2p::diagnostics::peer> plugin::api_impl::peers(filter value) const {
   return filter_peers(impl_->snapshot(), value);
}

fcl::p2p::diagnostics::peer plugin::api_impl::peer(fcl::p2p::peer_id value) const {
   auto values = peers(filter{.peer = std::move(value), .limit = 1});
   if (values.empty()) {
      FCL_THROW_EXCEPTION(exceptions::not_found, "P2P diagnostics peer was not found");
   }
   return std::move(values.front());
}

} // namespace fcl::plugins::p2p_diagnostics
