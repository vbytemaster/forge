module;

#include <mutex>
#include <memory>
#include <utility>
#include <vector>

module forge.app.diagnostics;

namespace forge::app {

struct diagnostics_store::impl {
   mutable std::mutex mutex;
   lifecycle_state state = lifecycle_state::created;
   std::string last_transition;
   std::string last_error;
   std::vector<plugin_diagnostics> plugins;
};

diagnostics_store::diagnostics_store() : impl_{std::make_shared<impl>()} {}

diagnostics_store::~diagnostics_store() = default;

void diagnostics_store::set_application_state(lifecycle_state state, std::string transition, std::string error) {
   const auto lock = std::scoped_lock{impl_->mutex};
   impl_->state = state;
   impl_->last_transition = std::move(transition);
   impl_->last_error = std::move(error);
}

void diagnostics_store::set_plugin_state(std::string id, std::string version, lifecycle_state state,
                                         std::string transition, std::string error) {
   const auto lock = std::scoped_lock{impl_->mutex};
   for (auto& plugin : impl_->plugins) {
      if (plugin.id == id) {
         plugin.version = std::move(version);
         plugin.state = state;
         plugin.last_transition = std::move(transition);
         plugin.last_error = std::move(error);
         return;
      }
   }
   impl_->plugins.push_back(plugin_diagnostics{
       .id = std::move(id),
       .version = std::move(version),
       .state = state,
       .last_transition = std::move(transition),
       .last_error = std::move(error),
   });
}

application_diagnostics_snapshot diagnostics_store::snapshot(const event_bus& events) const {
   auto out = application_diagnostics_snapshot{};
   {
      const auto lock = std::scoped_lock{impl_->mutex};
      out.state = impl_->state;
      out.last_transition = impl_->last_transition;
      out.last_error = impl_->last_error;
      out.plugins = impl_->plugins;
   }
   out.events = events.metrics();
   out.critical_events = events.critical_events();
   out.recent_events = events.recent_events();
   return out;
}

} // namespace forge::app
