module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <coroutine>
#include <exception>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module fcl.plugins.signature_provider.plugin;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.document;
import fcl.config.value;
import fcl.crypto.asymmetric;
import fcl.crypto.sha256;
import fcl.exceptions;
import fcl.plugins.signature_provider.api;
import fcl.plugins.signature_provider.exceptions;
import fcl.plugins.signature_provider.types;
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/signing_api.hxx"

namespace fcl::plugins::signature_provider {

plugin::plugin(plugin_options value) : impl_{std::make_shared<impl>(std::move(value))} {}

plugin::~plugin() = default;

fcl::app::plugin_descriptor descriptor(plugin_options value) {
   return fcl::app::plugin_descriptor{
      .id = {.value = "fcl.signature_provider"},
      .factory = [value = std::move(value)] { return std::make_unique<plugin>(value); },
   };
}

fcl::app::plugin_id plugin::id() const {
   return {.value = "fcl.signature_provider"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> plugin::describe_config() const {
   return fcl::config::describe_component<config>("signature-provider");
}

boost::asio::awaitable<void> plugin::configure(fcl::config::component_view view) {
   apply_config(*impl_, view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(fcl::api::provider& provider) {
   provider.install<api>(std::make_shared<signing_api>(impl_));
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

} // namespace fcl::plugins::signature_provider
