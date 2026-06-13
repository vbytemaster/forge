module;

#include <fcl/api/api_macros.hpp>

#include <vector>

export module fcl.plugins.p2p_diagnostics.api;

import fcl.api;
import fcl.p2p;
import fcl.plugins.p2p_diagnostics.types;

export namespace fcl::plugins::p2p_diagnostics {

class api : public fcl::api::contract<api> {
 public:
   virtual ~api() = default;

   [[nodiscard]] virtual fcl::p2p::diagnostics::snapshot snapshot() const = 0;
   [[nodiscard]] virtual fcl::p2p::diagnostics::snapshot
   snapshot(fcl::p2p::diagnostics::options options) const = 0;
   [[nodiscard]] virtual fcl::p2p::diagnostics::network_state network() const = 0;
   [[nodiscard]] virtual fcl::p2p::resource_manager::snapshot resources() const = 0;
   [[nodiscard]] virtual fcl::p2p::pubsub::snapshot pubsub() const = 0;
   [[nodiscard]] virtual std::vector<fcl::p2p::diagnostics::peer> peers(filter value = {}) const = 0;
   [[nodiscard]] virtual fcl::p2p::diagnostics::peer peer(fcl::p2p::peer_id value) const = 0;
};

} // namespace fcl::plugins::p2p_diagnostics

export {
FCL_API(::fcl::plugins::p2p_diagnostics::api, FCL_API_CONTRACT("fcl.plugins.p2p_diagnostics", 1, 0))
}
