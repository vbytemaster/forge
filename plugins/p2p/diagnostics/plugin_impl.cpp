module;

#include <fcl/exceptions/macros.hpp>

#include <memory>

module fcl.plugins.p2p.diagnostics.plugin;

import fcl.p2p.diagnostics;
import fcl.plugins.p2p.node.api;
import fcl.plugins.p2p.diagnostics.exceptions;
import fcl.plugins.p2p.diagnostics.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"

namespace fcl::plugins::p2p::diagnostics {

fcl::plugins::p2p::node::diagnostics_source& plugin::impl::require_source() const {
   if (!initialized || !source) {
      FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P diagnostics plugin is not initialized");
   }
   return *source;
}

fcl::p2p::diagnostics::snapshot plugin::impl::snapshot() const {
   return require_source().snapshot(configured_options(settings));
}

} // namespace fcl::plugins::p2p::diagnostics
