module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/macros.hpp>

#include <utility>

export module fcl.plugins.http_server.publisher;

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
import fcl.plugins.http_server.types;

export namespace fcl::plugins::http_server {

class publisher : public fcl::api::contract<publisher> {
 public:
   virtual ~publisher() = default;

   virtual void publish(fcl::http::api_binding binding, publish_options options = {}) = 0;

   template <typename Interface>
   void publish(fcl::api::binding_plan plan, publish_options options = {}) {
      auto binding = fcl::http::api().use(std::move(plan)).template bind<Interface>().build();
      publish(std::move(binding), std::move(options));
   }
};

} // namespace fcl::plugins::http_server

export {
FCL_API(::fcl::plugins::http_server::publisher, FCL_API_CONTRACT("fcl.plugins.http_server.publisher", 1, 0))
}
