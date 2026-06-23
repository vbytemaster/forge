module;

#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module forge.app.plugin_registry;

namespace forge::app {
namespace {

using descriptor_map = std::map<std::string, const plugin_descriptor*>;

[[nodiscard]] const plugin_descriptor& require_descriptor(const descriptor_map& descriptors, const plugin_id& id) {
   const auto iterator = descriptors.find(id.value);
   if (iterator == descriptors.end()) {
      throw std::logic_error{"missing app plugin dependency: " + id.value};
   }
   return *iterator->second;
}

[[nodiscard]] std::map<std::string, bool> explicit_config(const std::vector<plugin_config>& config) {
   auto out = std::map<std::string, bool>{};
   for (const auto& entry : config) {
      if (!valid_plugin_id(entry.id)) {
         throw std::invalid_argument{"app plugin config contains empty plugin id"};
      }
      out[entry.id.value] = entry.enabled;
   }
   return out;
}

void visit_plugin(const plugin_descriptor& descriptor, const descriptor_map& descriptors,
                  const std::map<std::string, bool>& explicit_values, std::set<std::string>& visiting,
                  std::set<std::string>& visited, std::vector<const plugin_descriptor*>& order) {
   if (visited.contains(descriptor.id.value)) {
      return;
   }
   if (visiting.contains(descriptor.id.value)) {
      throw std::logic_error{"cyclic app plugin dependency: " + descriptor.id.value};
   }
   if (const auto disabled = explicit_values.find(descriptor.id.value);
       disabled != explicit_values.end() && !disabled->second) {
      throw std::logic_error{"app plugin dependency is explicitly disabled: " + descriptor.id.value};
   }

   visiting.insert(descriptor.id.value);
   for (const auto& dependency : descriptor.dependencies) {
      visit_plugin(require_descriptor(descriptors, dependency), descriptors, explicit_values, visiting, visited, order);
   }
   visiting.erase(descriptor.id.value);
   visited.insert(descriptor.id.value);
   order.push_back(&descriptor);
}

} // namespace

void plugin_registry::register_plugin(plugin_descriptor descriptor) {
   if (!valid_plugin_id(descriptor.id)) {
      throw std::invalid_argument{"app plugin id must not be empty"};
   }
   if (!descriptor.factory) {
      throw std::invalid_argument{"app plugin factory must not be empty"};
   }
   for (const auto& existing : descriptors_) {
      if (existing.id == descriptor.id) {
         throw std::invalid_argument{"duplicate app plugin registered: " + descriptor.id.value};
      }
   }
   descriptors_.push_back(std::move(descriptor));
}

std::vector<std::unique_ptr<plugin>>
plugin_registry::instantiate_enabled(const std::vector<plugin_config>& config) const {
   const auto explicit_values = explicit_config(config);

   auto by_id = descriptor_map{};
   for (const auto& descriptor : descriptors_) {
      by_id.emplace(descriptor.id.value, &descriptor);
   }

   auto roots = std::vector<const plugin_descriptor*>{};
   for (const auto& descriptor : descriptors_) {
      const auto explicit_iterator = explicit_values.find(descriptor.id.value);
      const auto enabled =
          explicit_iterator != explicit_values.end() ? explicit_iterator->second : descriptor.enabled_by_default;
      if (enabled) {
         roots.push_back(&descriptor);
      }
   }

   auto visiting = std::set<std::string>{};
   auto visited = std::set<std::string>{};
   auto order = std::vector<const plugin_descriptor*>{};
   for (const auto* root : roots) {
      visit_plugin(*root, by_id, explicit_values, visiting, visited, order);
   }

   auto out = std::vector<std::unique_ptr<plugin>>{};
   out.reserve(order.size());
   for (const auto* descriptor : order) {
      auto instance = descriptor->factory();
      if (!instance) {
         throw std::logic_error{"app plugin factory returned null: " + descriptor->id.value};
      }
      if (instance->id() != descriptor->id) {
         throw std::logic_error{"app plugin instance id does not match descriptor: " + descriptor->id.value};
      }
      out.push_back(std::move(instance));
   }
   return out;
}

std::vector<plugin_descriptor> plugin_registry::descriptors() const {
   return descriptors_;
}

} // namespace forge::app
