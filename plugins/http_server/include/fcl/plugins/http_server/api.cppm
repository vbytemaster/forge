module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/macros.hpp>

#include <utility>

export module fcl.plugins.http_server.api;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.dispatcher;
import fcl.api.binding;
import fcl.http.api;
import fcl.plugins.http_server.middleware;
import fcl.plugins.http_server.types;

export namespace fcl::plugins::http_server {

class api : public fcl::api::contract<api, fcl::api::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<void> use(middleware_descriptor descriptor) = 0;

   template <typename Interface> boost::asio::awaitable<void> publish(publish_options options = {}) {
      co_await publish_binding(build_binding<Interface>(registry()), std::move(options));
   }

 private:
   template <typename Interface> static fcl::http::api_binding build_binding(const fcl::api::registry& registry) {
      auto plan = fcl::api::binding().serve(registry).build();
      return fcl::http::api().use(std::move(plan)).bind<Interface>().build();
   }

   [[nodiscard]] virtual const fcl::api::registry& registry() const = 0;
   virtual boost::asio::awaitable<void> publish_binding(fcl::http::api_binding binding,
                                                        publish_options options) = 0;
};

} // namespace fcl::plugins::http_server

export {
FCL_API(::fcl::plugins::http_server::api, FCL_API_CONTRACT("fcl.plugins.http_server", 1, 0))
}
