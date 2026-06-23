module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/macros.hpp>

#include <utility>
#include <vector>

export module forge.plugins.p2p.resolver.api;

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
import forge.p2p.protocol;
import forge.plugins.p2p.resolver.types;

export namespace forge::plugins::p2p::resolver {

class api : public forge::api::contract<api> {
 public:
   virtual ~api() = default;

   virtual void publish_api(forge::api::binding_plan plan, forge::p2p::protocol_id protocol,
                            publish_options options = {}) = 0;
   [[nodiscard]] virtual std::vector<entry> local_apis() const = 0;
   virtual boost::asio::awaitable<std::vector<entry>> peer_apis(forge::p2p::peer_id peer,
                                                                resolve_options options = {}) = 0;
   virtual boost::asio::awaitable<resolution> resolve(forge::p2p::peer_id peer, forge::api::api_ref api,
                                                      resolve_options options = {}) = 0;
   template <typename Interface>
   boost::asio::awaitable<forge::api::handle<Interface>> remote(forge::p2p::peer_id peer, resolve_options options = {}) {
      auto descriptor = Interface::describe();
      auto requested = forge::api::api_ref{.id = descriptor.id,
                                         .major = descriptor.version.major,
                                         .min_revision = descriptor.version.revision};
      auto resolved =
         co_await open_resolved_connection(std::move(peer), std::move(requested), std::move(descriptor), options);
      co_return co_await resolved.connection.template get_remote_api<Interface>(std::move(resolved.selected));
   }

 private:
   virtual boost::asio::awaitable<resolved_connection>
   open_resolved_connection(forge::p2p::peer_id peer, forge::api::api_ref api, forge::api::descriptor descriptor,
                            resolve_options options) = 0;
};

} // namespace forge::plugins::p2p::resolver

export {
FORGE_API(::forge::plugins::p2p::resolver::api, FORGE_API_CONTRACT("forge.plugins.p2p.resolver", 1, 0))
}
