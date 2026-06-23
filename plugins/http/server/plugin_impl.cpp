module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

module forge.plugins.http.server.plugin;

import forge.api.registry;
import forge.asio.runtime;
import forge.http.api.binding;
import forge.http.server;
import forge.plugins.http.server.exceptions;
import forge.plugins.http.server.middleware;
import forge.plugins.http.server.types;

#include "details/plugin_impl.hxx"

namespace forge::plugins::http::server {

void plugin::impl::add(pending_binding value) {
   auto lock = std::scoped_lock{mutex};
   if (publication_closed) {
      FORGE_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server publication is closed");
   }
   bindings.push_back(std::move(value));
}

void plugin::impl::add(middleware_descriptor value) {
   auto lock = std::scoped_lock{mutex};
   if (publication_closed) {
      FORGE_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server publication is closed");
   }
   middleware.push_back(std::move(value));
}

startup_snapshot plugin::impl::close_publication() {
   auto lock = std::scoped_lock{mutex};
   publication_closed = true;
   return startup_snapshot{
      .bindings = std::move(bindings),
      .middleware = std::move(middleware),
   };
}

void plugin::impl::reset_runtime() noexcept {
   auto lock = std::scoped_lock{mutex};
   apis = nullptr;
   runtime = nullptr;
   publication_closed = false;
   bindings.clear();
   middleware.clear();
}

} // namespace forge::plugins::http::server
