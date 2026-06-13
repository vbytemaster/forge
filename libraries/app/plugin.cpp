module;

#include <boost/asio/awaitable.hpp>

#include <optional>

module fcl.app.plugin;

import fcl.config.component;
import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;

namespace fcl::app {

std::optional<config::component_descriptor> plugin::describe_config() const {
   return std::nullopt;
}

boost::asio::awaitable<void> plugin::configure(config::component_view) {
   co_return;
}

boost::asio::awaitable<void> plugin::provide(fcl::api::provider&) {
   co_return;
}

void plugin::request_stop() noexcept {}

bool valid_plugin_id(const plugin_id& id) noexcept {
   return !id.value.empty();
}

} // namespace fcl::app
