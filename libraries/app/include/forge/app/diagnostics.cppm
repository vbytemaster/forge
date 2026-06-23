module;

#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module forge.app.diagnostics;

import forge.app.events;

export namespace forge::app {

enum class lifecycle_state {
   created,
   initializing,
   initialized,
   starting,
   started,
   stopping,
   stopped,
   failed,
};

struct plugin_diagnostics {
   std::string id;
   std::string version;
   lifecycle_state state = lifecycle_state::created;
   std::string last_transition;
   std::string last_error;
};

struct application_diagnostics_snapshot {
   lifecycle_state state = lifecycle_state::created;
   std::string last_transition;
   std::string last_error;
   std::vector<plugin_diagnostics> plugins;
   event_bus_metrics events;
   std::vector<event_record> critical_events;
   std::vector<event_record> recent_events;
};

class diagnostics_store {
 public:
   diagnostics_store();
   ~diagnostics_store();

   diagnostics_store(const diagnostics_store&) = delete;
   diagnostics_store& operator=(const diagnostics_store&) = delete;

   void set_application_state(lifecycle_state state, std::string transition, std::string error = {});
   void set_plugin_state(std::string id, std::string version, lifecycle_state state, std::string transition,
                         std::string error = {});
   [[nodiscard]] application_diagnostics_snapshot snapshot(const event_bus& events) const;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::app
