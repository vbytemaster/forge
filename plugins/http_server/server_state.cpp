module;

#include <boost/asio/awaitable.hpp>

#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

module fcl.plugins.http_server.plugin;

import fcl.api.registry;
import fcl.asio.runtime;
import fcl.http.api;
import fcl.http.middleware;
import fcl.http.router;
import fcl.http.server;
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.types;

#include "details/config.hxx"
#include "details/state.hxx"
#include "details/server_state.hxx"

namespace fcl::plugins::http_server {

boost::asio::awaitable<void> start_server(plugin::impl& state) {
   if (state.runtime == nullptr || state.apis == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::startup_failed, "HTTP server plugin is not initialized");
   }

   auto snapshot = state.close_publication();
   auto router = fcl::http::router{};
   for (auto& descriptor : snapshot.middleware) {
      router.use(std::move(descriptor));
   }
   for (auto& value : snapshot.publications) {
      auto binding = value.build(*state.apis);
      binding.mount(router, resolve_base_path(state.settings, value.options().base_path));
   }

   auto server = std::make_unique<fcl::http::server>(*state.runtime, to_server_config(state.settings), std::move(router));
   co_await server->async_start();

   auto lock = std::scoped_lock{state.mutex};
   state.server = std::move(server);
}

boost::asio::awaitable<void> stop_server(plugin::impl& state) {
   auto server = std::unique_ptr<fcl::http::server>{};
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

} // namespace fcl::plugins::http_server
