module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.http_server.plugin;

import fcl.api.registry;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.asio.runtime;
import fcl.config.component;
import fcl.config.decode;
import fcl.http.api;
import fcl.http.middleware;
import fcl.http.server;
import fcl.plugins.http_server.api;
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/publisher_api.hxx"
#include "details/server_lifecycle.hxx"

namespace fcl::plugins::http_server {

plugin::plugin() : impl_{std::make_shared<impl>()} {}

plugin::~plugin() = default;

fcl::app::plugin_id plugin::id() const {
   return fcl::app::plugin_id{.value = "fcl.http_server"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> plugin::describe_config() const {
   return fcl::config::describe_component<config>("http-server");
}

boost::asio::awaitable<void> plugin::configure(fcl::config::component_view view) {
   impl_->settings = decode_config(view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(fcl::api::provider& provider) {
   provider.install<api>(std::make_shared<publisher_api>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(fcl::app::plugin_context& context) {
   impl_->runtime = &context.scheduler().runtime_context();
   impl_->apis = &context.apis().registry_ref();
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   co_await start_server(*impl_);
}

void plugin::request_stop() noexcept {
   request_server_stop(*impl_);
}

boost::asio::awaitable<void> plugin::shutdown() {
   impl_->stopping = true;
   co_await stop_server(*impl_);
   impl_->reset_runtime();
}

fcl::app::plugin_descriptor descriptor() {
   return fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "fcl.http_server"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace fcl::plugins::http_server
