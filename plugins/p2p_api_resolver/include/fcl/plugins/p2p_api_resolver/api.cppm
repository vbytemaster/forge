module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/api_macros.hpp>

#include <utility>
#include <vector>

export module fcl.plugins.p2p_api_resolver.api;

import fcl.api;
import fcl.p2p;
import fcl.plugins.p2p_api_resolver.types;

export namespace fcl::plugins::p2p_api_resolver {

class api : public fcl::api::contract<api> {
 public:
   virtual ~api() = default;

   virtual void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol,
                            publish_options options = {}) = 0;
   [[nodiscard]] virtual std::vector<entry> local_apis() const = 0;
   virtual boost::asio::awaitable<std::vector<entry>> peer_apis(fcl::p2p::peer_id peer,
                                                                resolve_options options = {}) = 0;
   virtual boost::asio::awaitable<resolution> resolve(fcl::p2p::peer_id peer, fcl::api::api_ref api,
                                                      resolve_options options = {}) = 0;
   template <typename Interface>
   boost::asio::awaitable<fcl::api::handle<Interface>> remote(fcl::p2p::peer_id peer, resolve_options options = {}) {
      auto descriptor = Interface::describe();
      auto requested = fcl::api::api_ref{.id = descriptor.id,
                                         .major = descriptor.version.major,
                                         .min_revision = descriptor.version.revision};
      auto resolved =
         co_await open_resolved_connection(std::move(peer), std::move(requested), std::move(descriptor), options);
      co_return co_await resolved.connection.template get_remote_api<Interface>(std::move(resolved.selected));
   }

 private:
   virtual boost::asio::awaitable<resolved_connection>
   open_resolved_connection(fcl::p2p::peer_id peer, fcl::api::api_ref api, fcl::api::descriptor descriptor,
                            resolve_options options) = 0;
};

} // namespace fcl::plugins::p2p_api_resolver

export {
FCL_API(::fcl::plugins::p2p_api_resolver::api, FCL_API_CONTRACT("fcl.plugins.p2p_api_resolver", 1, 0))
}
