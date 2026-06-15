module;

#include <fcl/exceptions/macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.plugins.http_server.plugin;

import fcl.asio.runtime;
import fcl.exceptions;
import fcl.http.api;
import fcl.http.exceptions;
import fcl.http.middleware;
import fcl.http.router;
import fcl.http.server;
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.types;

#include "private/base_path.hxx"
#include "private/publication_store.hxx"
#include "private/server_state.hxx"

namespace fcl::plugins::http_server::detail {

server_state::server_state(config settings) : settings_{std::move(settings)} {}

server_state::~server_state() = default;

void server_state::set_runtime(fcl::asio::runtime& runtime) noexcept {
   runtime_ = &runtime;
}

boost::asio::awaitable<void> server_state::start(publication_snapshot snapshot) {
   if (runtime_ == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "HTTP server plugin is not initialized");
   }
   if (server_) {
      co_return;
   }

   auto router = fcl::http::router{};
   try {
      for (auto& descriptor : snapshot.middleware) {
         router.use(std::move(descriptor));
      }
      for (auto& publication : snapshot.publications) {
         const auto base_path = publication.options.base_path.empty() ? settings_.api_base_path
                                                                      : publication.options.base_path;
         publication.binding.mount(router, base_path == "/" ? std::string_view{} : std::string_view{base_path});
      }
   } catch (const fcl::http::exceptions::conflict& error) {
      FCL_THROW_EXCEPTION(exceptions::route_conflict, error.message());
   }

   auto server_config = fcl::http::server_config{
      .bind_address = settings_.bind_address,
      .port = static_cast<std::uint16_t>(settings_.port),
      .max_request_body_bytes = settings_.max_request_body_bytes,
      .max_header_bytes = settings_.max_header_bytes,
      .read_timeout = std::chrono::milliseconds{settings_.read_timeout_ms},
      .idle_timeout = std::chrono::milliseconds{settings_.idle_timeout_ms},
   };
   server_ = std::make_unique<fcl::http::server>(*runtime_, std::move(server_config), std::move(router));
   co_await server_->async_start();
}

void server_state::request_stop() noexcept {
   if (server_) {
      server_->stop();
   }
}

boost::asio::awaitable<void> server_state::stop() {
   if (server_) {
      co_await server_->async_stop();
      server_.reset();
   }
}

} // namespace fcl::plugins::http_server::detail
