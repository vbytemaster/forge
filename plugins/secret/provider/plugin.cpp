module;

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

module fcl.plugins.secret.provider.plugin;

import fcl.api.registry;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.config.decode;
import fcl.crypto.secret_bytes;
import fcl.plugins.secret.provider.api;
import fcl.plugins.secret.provider.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/secret_api.hxx"

namespace fcl::plugins::secret::provider {

plugin::plugin() : impl_{std::make_shared<impl>()} {}

plugin::~plugin() = default;

fcl::app::plugin_descriptor descriptor() {
   return fcl::app::plugin_descriptor{
      .id = {.value = "fcl.plugins.secret.provider"},
      .factory = [] { return std::make_unique<plugin>(); },
   };
}

fcl::app::plugin_id plugin::id() const {
   return {.value = "fcl.plugins.secret.provider"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> plugin::describe_config() const {
   return fcl::config::describe_component<config>("plugins.secret.provider");
}

boost::asio::awaitable<void> plugin::configure(fcl::config::component_view view) {
   apply_config(*impl_, view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(fcl::api::provider& provider) {
   provider.install<api>(std::make_shared<secret_api>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(fcl::app::plugin_context&) {
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

} // namespace fcl::plugins::secret::provider
