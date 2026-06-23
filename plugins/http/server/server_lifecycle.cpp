module;

#include <boost/asio/awaitable.hpp>

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

module forge.plugins.http.server.plugin;

import forge.api.registry;
import forge.asio.runtime;
import forge.http.api.binding;
import forge.http.middleware;
import forge.http.router;
import forge.http.server;
import forge.plugins.http.server.exceptions;
import forge.plugins.http.server.middleware;
import forge.plugins.http.server.types;

#include "details/config.hxx"
#include "details/middleware_bridge.hxx"
#include "details/plugin_impl.hxx"
#include "details/server_lifecycle.hxx"

namespace forge::plugins::http::server {

boost::asio::awaitable<void> start_server(plugin::impl& state) {
   if (state.runtime == nullptr || state.apis == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::startup_failed, "HTTP server plugin is not initialized");
   }

   auto snapshot = state.close_publication();
   auto router = forge::http::router{};
   for (auto& descriptor : snapshot.middleware) {
      router.use(to_http_middleware(std::move(descriptor)));
   }
   for (auto& value : snapshot.bindings) {
      value.binding.mount(router, resolve_base_path(state.settings, value.options.base_path));
   }

   auto server = std::make_unique<forge::http::server>(*state.runtime, to_server_config(state.settings), std::move(router));
   co_await server->async_start();

   auto lock = std::scoped_lock{state.mutex};
   state.server = std::move(server);
}

boost::asio::awaitable<void> stop_server(plugin::impl& state) {
   auto server = std::unique_ptr<forge::http::server>{};
   {
      auto lock = std::scoped_lock{state.mutex};
      server = std::move(state.server);
   }
   if (server) {
      co_await server->async_stop();
   }
}

void request_server_stop(plugin::impl& state) noexcept {
   auto lock = std::scoped_lock{state.mutex};
   state.stopping = true;
}

} // namespace forge::plugins::http::server
