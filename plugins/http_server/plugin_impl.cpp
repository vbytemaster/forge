module;

#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

module fcl.plugins.http_server.plugin;

import fcl.api.registry;
import fcl.asio.runtime;
import fcl.http.api.binding;
import fcl.http.server;
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.middleware;
import fcl.plugins.http_server.types;

#include "details/plugin_impl.hxx"

namespace fcl::plugins::http_server {

void plugin::impl::add(pending_binding value) {
   auto lock = std::scoped_lock{mutex};
   if (publication_closed) {
      FCL_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server publication is closed");
   }
   bindings.push_back(std::move(value));
}

void plugin::impl::add(middleware_descriptor value) {
   auto lock = std::scoped_lock{mutex};
   if (publication_closed) {
      FCL_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server publication is closed");
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

} // namespace fcl::plugins::http_server
