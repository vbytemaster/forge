module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.net.p2p.endpoint;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.peer_store;

#include "details/host_addresses.hxx"
#include "details/peer_exchange_learning.hxx"

namespace forge::net::p2p::detail {

void learn_authenticated_peer_exchange_response(peer_store& store, const peer_exchange_message& message,
                                                const peer_id& authenticated_peer,
                                                std::optional<forge::net::p2p::endpoint> remote_endpoint) {
   if (!valid_peer_id(authenticated_peer) || message.peer != authenticated_peer) {
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed,
                            "P2P peer exchange response identity does not match authenticated session");
   }
   store.upsert(peer_store::record{
       .peer = authenticated_peer,
       .capabilities = message.capabilities,
   });
   for (const auto& endpoint : message.endpoints) {
      if (!valid_peer_id(endpoint.peer)) {
         continue;
      }
      const auto from_sender = endpoint.peer == authenticated_peer;
      auto context = host_addresses::learning_context{
          .source = from_sender ? host_addresses::source_kind::authenticated : host_addresses::source_kind::third_party,
      };
      if (from_sender) {
         context.remote_endpoint = remote_endpoint;
      }
      if (auto learned = host_addresses::learned(endpoint.endpoint, endpoint.peer, context)) {
         store.learn_endpoint(endpoint.peer, *learned, endpoint.capabilities);
      }
   }
}

} // namespace forge::net::p2p::detail
