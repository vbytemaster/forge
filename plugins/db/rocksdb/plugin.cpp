module;
#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

module forge.plugins.db.rocksdb.plugin;

import forge.api.binding;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.asio.task_scheduler;
import forge.config.component;
import forge.rocksdb.store;

#include "details/plugin_impl.hxx"

namespace forge::plugins::db::rocksdb {

plugin::plugin() : impl_{detail::lifecycle::make_impl()} {}
plugin::~plugin() = default;

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.db.rocksdb"};
}


std::string plugin::version() const {
   return "1.0.0";
}


std::optional<forge::config::component_descriptor> plugin::describe_config() const {
   return detail::lifecycle::describe_config(impl_);
}

boost::asio::awaitable<void> plugin::configure(forge::config::component_view view) {
   return detail::lifecycle::configure(impl_, view);
}

boost::asio::awaitable<void> plugin::provide(forge::api::provider& provider) {
   return detail::lifecycle::provide(impl_, provider);
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context& context) {
   return detail::lifecycle::initialize(impl_, context);
}

boost::asio::awaitable<void> plugin::startup() {
   return detail::lifecycle::startup(impl_);
}

void plugin::request_stop() noexcept {
   detail::lifecycle::request_stop(impl_);
}

boost::asio::awaitable<void> plugin::shutdown() {
   return detail::lifecycle::shutdown(impl_);
}

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "forge.plugins.db.rocksdb"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}


} // namespace forge::plugins::db::rocksdb
