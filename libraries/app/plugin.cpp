module;

#include <boost/asio/awaitable.hpp>

#include <optional>

module fcl.app.plugin;

import fcl.config.component;
import fcl.api;

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
