module;

#include <forge/api/macros.hpp>

#include <vector>

export module forge.plugins.p2p.diagnostics.api;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.p2p.identity;
import forge.p2p.diagnostics;
import forge.p2p.pubsub;
import forge.p2p.resource_manager;
import forge.plugins.p2p.diagnostics.types;

export namespace forge::plugins::p2p::diagnostics {

class api : public forge::api::contract<api> {
 public:
   virtual ~api() = default;

   [[nodiscard]] virtual forge::p2p::diagnostics::snapshot snapshot() const = 0;
   [[nodiscard]] virtual forge::p2p::diagnostics::snapshot
   snapshot(forge::p2p::diagnostics::options options) const = 0;
   [[nodiscard]] virtual forge::p2p::diagnostics::network_state network() const = 0;
   [[nodiscard]] virtual forge::p2p::resource_manager::snapshot resources() const = 0;
   [[nodiscard]] virtual forge::p2p::pubsub::snapshot pubsub() const = 0;
   [[nodiscard]] virtual std::vector<forge::p2p::diagnostics::peer> peers(filter value = {}) const = 0;
   [[nodiscard]] virtual forge::p2p::diagnostics::peer peer(forge::p2p::peer_id value) const = 0;
};

} // namespace forge::plugins::p2p::diagnostics

export {
FORGE_API(::forge::plugins::p2p::diagnostics::api, FORGE_API_CONTRACT("forge.plugins.p2p.diagnostics", 1, 0))
}
