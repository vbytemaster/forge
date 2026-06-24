module;

#include <optional>
#include <string>
#include <utility>

module forge.app.plugin_context;

namespace forge::app {
namespace {

forge::api::registry& default_api_registry() {
   static auto registry = forge::api::registry{};
   return registry;
}

view_registry& default_view_registry() {
   static auto registry = view_registry{};
   return registry;
}

} // namespace

plugin_context::plugin_context(forge::asio::task_scheduler& scheduler, forge::api::registry& apis, signal_bus& signals,
                               event_bus& events, diagnostics_store* diagnostics, config_view config,
                               view_registry* views)
    : scheduler_{&scheduler}, apis_{&apis}, signals_{&signals}, events_{&events}, diagnostics_{diagnostics},
      views_{views == nullptr ? &default_view_registry() : views}, config_{std::move(config)} {}

plugin_context::plugin_context(forge::asio::task_scheduler& scheduler, signal_bus& signals, event_bus& events,
                               diagnostics_store* diagnostics, config_view config, view_registry* views)
    : plugin_context{scheduler, default_api_registry(), signals, events, diagnostics, std::move(config), views} {}

forge::asio::task_scheduler& plugin_context::scheduler() noexcept {
   return *scheduler_;
}

forge::api::view plugin_context::apis() const noexcept {
   return forge::api::view{*apis_};
}

signal_bus& plugin_context::signals() noexcept {
   return *signals_;
}

event_bus& plugin_context::events() noexcept {
   return *events_;
}

diagnostics_store* plugin_context::diagnostics() noexcept {
   return diagnostics_;
}

view_registry& plugin_context::views() noexcept {
   return *views_;
}

const config_view& plugin_context::config() const noexcept {
   return config_;
}

std::optional<std::string> plugin_context::config_value(const std::string& key) const {
   const auto iterator = config_.find(key);
   if (iterator == config_.end()) {
      return std::nullopt;
   }
   return iterator->second;
}

} // namespace forge::app
