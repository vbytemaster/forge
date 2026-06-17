module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <mutex>
#include <typeindex>
#include <utility>
#include <vector>

module fcl.plugins.http_server.plugin;

import fcl.api.registry;
import fcl.asio.runtime;
import fcl.http.api;
import fcl.http.middleware;
import fcl.http.server;
import fcl.plugins.http_server.api;
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.types;

#include "details/plugin_impl.hxx"
#include "details/publisher_api.hxx"

namespace fcl::plugins::http_server {

plugin::publisher_api::publisher_api(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

const fcl::api::registry& plugin::publisher_api::registry() const {
   if (impl_->apis == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::startup_failed, "HTTP server plugin is not initialized");
   }
   return *impl_->apis;
}

boost::asio::awaitable<void> plugin::publisher_api::publish_typed(
   std::type_index interface_type,
   publish_options options,
   std::shared_ptr<void> (*factory)(const fcl::api::registry&)) {
   static_cast<void>(interface_type);
   auto binding = std::static_pointer_cast<fcl::http::api_binding>(factory(registry()));
   impl_->add(pending_api_binding{.binding = std::move(*binding), .options = std::move(options)});
   co_return;
}

boost::asio::awaitable<void> plugin::publisher_api::use(fcl::http::middleware_descriptor descriptor) {
   impl_->add(std::move(descriptor));
   co_return;
}

} // namespace fcl::plugins::http_server
