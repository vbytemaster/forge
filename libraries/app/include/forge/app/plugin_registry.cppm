module;

#include <functional>
#include <memory>
#include <vector>

export module forge.app.plugin_registry;

import forge.app.plugin;

export namespace forge::app {

using plugin_factory = std::function<std::unique_ptr<plugin>()>;

struct plugin_descriptor {
   plugin_id id;
   std::vector<plugin_id> dependencies;
   bool enabled_by_default = true;
   plugin_factory factory;
};

class plugin_registry {
 public:
   void register_plugin(plugin_descriptor descriptor);
   [[nodiscard]] std::vector<std::unique_ptr<plugin>>
   instantiate_enabled(const std::vector<plugin_config>& config) const;
   [[nodiscard]] std::vector<plugin_descriptor> descriptors() const;

 private:
   std::vector<plugin_descriptor> descriptors_;
};

} // namespace forge::app
