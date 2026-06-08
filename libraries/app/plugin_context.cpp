module;

#include <optional>
#include <string>
#include <utility>

module fcl.app.plugin_context;

namespace fcl::app {
namespace {

fcl::api::registry& default_api_registry() {
   static auto registry = fcl::api::registry{};
   return registry;
}

} // namespace

plugin_context::plugin_context(fcl::asio::task_scheduler& scheduler, fcl::api::registry& apis, signal_bus& signals,
                               event_bus& events, diagnostics_store* diagnostics, config_view config)
    : scheduler_{&scheduler}, apis_{&apis}, signals_{&signals}, events_{&events}, diagnostics_{diagnostics},
      config_{std::move(config)} {}

plugin_context::plugin_context(fcl::asio::task_scheduler& scheduler, signal_bus& signals, event_bus& events,
                               diagnostics_store* diagnostics, config_view config)
    : plugin_context{scheduler, default_api_registry(), signals, events, diagnostics, std::move(config)} {}

fcl::asio::task_scheduler& plugin_context::scheduler() noexcept {
   return *scheduler_;
}

fcl::api::view plugin_context::apis() const noexcept {
   return fcl::api::view{*apis_};
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

} // namespace fcl::app
