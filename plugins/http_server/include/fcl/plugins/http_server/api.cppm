module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/macros.hpp>

#include <memory>
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
import fcl.http.api.binding;
import fcl.plugins.http_server.middleware;
import fcl.plugins.http_server.types;

export namespace fcl::plugins::http_server {

class api : public fcl::api::contract<api, fcl::api::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<void> use(middleware_descriptor descriptor) = 0;

   template <typename Interface> boost::asio::awaitable<void> publish(publish_options options = {}) {
      co_await publish(std::make_unique<typed_binding_spec<Interface>>(), std::move(options));
   }

 protected:
   class binding_spec {
    public:
      virtual ~binding_spec() = default;
      [[nodiscard]] virtual fcl::http::api::binding_plan build(const fcl::api::registry& registry) const = 0;
   };

 private:
   template <typename Interface> class typed_binding_spec final : public binding_spec {
    public:
      [[nodiscard]] fcl::http::api::binding_plan build(const fcl::api::registry& registry) const override {
         auto plan = fcl::api::binding().serve(registry).build();
         return fcl::http::api::binding().use(std::move(plan)).bind<Interface>().build();
      }
   };

   [[nodiscard]] virtual const fcl::api::registry& registry() const = 0;
   virtual boost::asio::awaitable<void> publish(std::unique_ptr<binding_spec> binding,
                                                publish_options options) = 0;
};

} // namespace fcl::plugins::http_server

export {
FCL_API(::fcl::plugins::http_server::api, FCL_API_CONTRACT("fcl.plugins.http_server", 1, 0))
}
