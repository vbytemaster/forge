module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string>

module forge.plugins.log.otlp.plugin;

import forge.api.registry;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.config.component;
import forge.config.decode;
import forge.log.log_message;
import forge.log.logger;
import forge.otlp.crash;
import forge.otlp.log_exporter;
import forge.otlp.log_sink;
import forge.plugins.log.otlp.api;
import forge.plugins.log.otlp.exceptions;
import forge.plugins.log.otlp.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/management_api.hxx"

namespace forge::plugins::log::otlp {

plugin::plugin() : impl_{std::make_shared<impl>()} {}

plugin::~plugin() = default;

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.log.otlp"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<forge::config::component_descriptor> plugin::describe_config() const {
   return forge::config::describe_component<config>("plugins.log.otlp");
}

boost::asio::awaitable<void> plugin::configure(forge::config::component_view view) {
   impl_->settings = decode_config(view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::provider& provider) {
   provider.install<api>(std::make_shared<management_api>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context& context) {
   impl_->runtime = &context.scheduler().runtime_context();
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   co_await start_exporter(*impl_);
}

void plugin::request_stop() noexcept {
   request_exporter_stop(*impl_);
}

boost::asio::awaitable<void> plugin::shutdown() {
   co_await stop_exporter(*impl_);
   impl_->runtime = nullptr;
}

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "forge.plugins.log.otlp"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace forge::plugins::log::otlp
