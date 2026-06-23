module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

module forge.plugins.http.server.plugin;

import forge.api.registry;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.asio.runtime;
import forge.config.component;
import forge.config.decode;
import forge.http.api.binding;
import forge.http.server;
import forge.plugins.http.server.api;
import forge.plugins.http.server.exceptions;
import forge.plugins.http.server.middleware;
import forge.plugins.http.server.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/publisher_api.hxx"
#include "details/server_lifecycle.hxx"

namespace forge::plugins::http::server {

plugin::plugin() : impl_{std::make_shared<impl>()} {}

plugin::~plugin() = default;

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.http.server"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<forge::config::component_descriptor> plugin::describe_config() const {
   return forge::config::describe_component<config>("plugins.http.server");
}

boost::asio::awaitable<void> plugin::configure(forge::config::component_view view) {
   impl_->settings = decode_config(view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::provider& provider) {
   provider.install<api>(std::make_shared<publisher_api>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context& context) {
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

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "forge.plugins.http.server"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace forge::plugins::http::server
