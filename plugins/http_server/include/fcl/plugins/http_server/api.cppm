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
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.http.api;
import fcl.http.middleware;
import fcl.plugins.http_server.types;

export namespace fcl::plugins::http_server {

class api : public fcl::api::contract<api, fcl::api::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<void> publish(publication value) = 0;
   virtual boost::asio::awaitable<void> publish(fcl::http::api_binding binding, publish_options options = {}) = 0;
   virtual boost::asio::awaitable<void> use(fcl::http::middleware_descriptor descriptor) = 0;

   template <typename Interface> boost::asio::awaitable<void> publish(publish_options options = {}) {
      auto value = publication{
         .build = [](const fcl::api::registry& apis) {
            auto plan = fcl::api::binding().serve(apis).build();
            return fcl::http::api().use(std::move(plan)).bind<Interface>().build();
         },
         .options = std::move(options),
      };
      co_await publish(std::move(value));
   }
};

} // namespace fcl::plugins::http_server

export {
FCL_API(::fcl::plugins::http_server::api, FCL_API_CONTRACT("fcl.plugins.http_server", 1, 0))
}
