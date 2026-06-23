module;

#include <boost/asio/awaitable.hpp>

#include <optional>

module forge.app.plugin;

import forge.config.component;
import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;

namespace forge::app {

std::optional<config::component_descriptor> plugin::describe_config() const {
   return std::nullopt;
}

boost::asio::awaitable<void> plugin::configure(config::component_view) {
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::provider&) {
   co_return;
}

void plugin::request_stop() noexcept {}

bool valid_plugin_id(const plugin_id& id) noexcept {
   return !id.value.empty();
}

} // namespace forge::app
