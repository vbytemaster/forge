module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/macros.hpp>

#include <memory>
#include <utility>

export module forge.plugins.http.server.api;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.dispatcher;
import forge.api.binding;
import forge.http.api.binding;
import forge.plugins.http.server.middleware;
import forge.plugins.http.server.types;

export namespace forge::plugins::http::server {

class api : public forge::api::contract<api, forge::api::surface::local> {
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
      [[nodiscard]] virtual forge::http::api::binding_plan build(const forge::api::registry& registry) const = 0;
   };

 private:
   template <typename Interface> class typed_binding_spec final : public binding_spec {
    public:
      [[nodiscard]] forge::http::api::binding_plan build(const forge::api::registry& registry) const override {
         auto plan = forge::api::binding().serve(registry).build();
         return forge::http::api::binding().use(std::move(plan)).bind<Interface>().build();
      }
   };

   [[nodiscard]] virtual const forge::api::registry& registry() const = 0;
   virtual boost::asio::awaitable<void> publish(std::unique_ptr<binding_spec> binding,
                                                publish_options options) = 0;
};

} // namespace forge::plugins::http::server

export {
FORGE_API(::forge::plugins::http::server::api, FORGE_API_CONTRACT("forge.plugins.http.server", 1, 0))
}
