module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <memory>
#include <utility>

module forge.plugins.http.server.plugin;

import forge.api.registry;
import forge.asio.runtime;
import forge.http.api.binding;
import forge.http.server;
import forge.plugins.http.server.api;
import forge.plugins.http.server.exceptions;
import forge.plugins.http.server.middleware;
import forge.plugins.http.server.types;

#include "details/plugin_impl.hxx"
#include "details/publisher_api.hxx"

namespace forge::plugins::http::server {

plugin::publisher_api::publisher_api(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

const forge::api::registry& plugin::publisher_api::registry() const {
   if (impl_->apis == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::startup_failed, "HTTP server plugin is not initialized");
   }
   return *impl_->apis;
}

boost::asio::awaitable<void> plugin::publisher_api::publish(std::unique_ptr<binding_spec> binding,
                                                            publish_options options) {
   impl_->add(pending_binding{.binding = binding->build(registry()), .options = std::move(options)});
   co_return;
}

boost::asio::awaitable<void> plugin::publisher_api::use(middleware_descriptor descriptor) {
   impl_->add(std::move(descriptor));
   co_return;
}

} // namespace forge::plugins::http::server
