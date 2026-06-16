module;

#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

module fcl.plugins.http_server.plugin;

import fcl.api.registry;
import fcl.asio.runtime;
import fcl.http.middleware;
import fcl.http.server;
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.types;

#include "details/plugin_impl.hxx"

namespace fcl::plugins::http_server {

void plugin::impl::add(publication value) {
   auto lock = std::scoped_lock{mutex};
   if (publication_closed) {
      FCL_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server publication is closed");
   }
   publications.push_back(std::move(value));
}

void plugin::impl::add(fcl::http::middleware_descriptor value) {
   auto lock = std::scoped_lock{mutex};
   if (publication_closed) {
      FCL_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server publication is closed");
   }
   middleware.push_back(std::move(value));
}

publication_snapshot plugin::impl::close_publication() {
   auto lock = std::scoped_lock{mutex};
   publication_closed = true;
   return publication_snapshot{
      .publications = std::move(publications),
      .middleware = std::move(middleware),
   };
}

void plugin::impl::reset_runtime() noexcept {
   auto lock = std::scoped_lock{mutex};
   apis = nullptr;
   runtime = nullptr;
   publication_closed = false;
   publications.clear();
   middleware.clear();
}

} // namespace fcl::plugins::http_server
