module;

#include <forge/exceptions/macros.hpp>

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

module forge.plugins.crypto.signer.plugin;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.config.component;
import forge.config.decode;
import forge.config.document;
import forge.config.value;
import forge.crypto.asymmetric;
import forge.crypto.sha256;
import forge.exceptions;
import forge.plugins.crypto.signer.api;
import forge.plugins.crypto.signer.exceptions;
import forge.plugins.crypto.signer.types;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/signer_api.hxx"

namespace forge::plugins::crypto::signer {

plugin::plugin(plugin_options value) : impl_{std::make_shared<impl>(std::move(value))} {}

plugin::~plugin() = default;

forge::app::plugin_descriptor descriptor(plugin_options value) {
   return forge::app::plugin_descriptor{
      .id = {.value = "forge.plugins.crypto.signer"},
      .factory = [value = std::move(value)] { return std::make_unique<plugin>(value); },
   };
}

forge::app::plugin_id plugin::id() const {
   return {.value = "forge.plugins.crypto.signer"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<forge::config::component_descriptor> plugin::describe_config() const {
   return forge::config::describe_component<config>("plugins.crypto.signer");
}

boost::asio::awaitable<void> plugin::configure(forge::config::component_view view) {
   apply_config(*impl_, view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::provider& provider) {
   provider.install<api>(std::make_shared<signer_api>(impl_));
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

} // namespace forge::plugins::crypto::signer
