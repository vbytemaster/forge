module;

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

module forge.plugins.crypto.secrets.plugin;

import forge.api.registry;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.config.component;
import forge.config.decode;
import forge.crypto.secret_bytes;
import forge.plugins.crypto.secrets.api;
import forge.plugins.crypto.secrets.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/secret_api.hxx"

namespace forge::plugins::crypto::secrets {

plugin::plugin() : impl_{std::make_shared<impl>()} {}

plugin::~plugin() = default;

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = {.value = "forge.plugins.crypto.secrets"},
      .factory = [] { return std::make_unique<plugin>(); },
   };
}

forge::app::plugin_id plugin::id() const {
   return {.value = "forge.plugins.crypto.secrets"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<forge::config::component_descriptor> plugin::describe_config() const {
   return forge::config::describe_component<config>("plugins.crypto.secrets");
}

boost::asio::awaitable<void> plugin::configure(forge::config::component_view view) {
   apply_config(*impl_, view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::provider& provider) {
   provider.install<api>(std::make_shared<secret_api>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context&) {
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   co_return;
}

void plugin::request_stop() noexcept {
   impl_->stopping = true;
}

boost::asio::awaitable<void> plugin::shutdown() {
   impl_->stopping = true;
   co_return;
}

} // namespace forge::plugins::crypto::secrets
