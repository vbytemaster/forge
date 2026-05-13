module;

#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

module fcl.config.component;

import fcl.config.document;
import fcl.config.value;

namespace fcl::config {

void component_registry::add(component_descriptor descriptor) {
   auto path_for = [](const component_descriptor& component, std::string_view field) {
      auto path = component.section;
      if (!path.empty()) {
         path += ".";
      }
      path += field;
      return path;
   };
   auto known = std::set<std::string>{};
   for (const auto& existing : components_) {
      for (const auto& field : existing.fields) {
         known.insert(path_for(existing, field.name));
         for (const auto& alias : field.aliases) {
            known.insert(path_for(existing, alias));
         }
      }
   }
   for (const auto& field : descriptor.fields) {
      const auto canonical = path_for(descriptor, field.name);
      if (!known.insert(canonical).second) {
         throw std::invalid_argument{"duplicate config field: " + canonical};
      }
      for (const auto& alias : field.aliases) {
         const auto alias_path = path_for(descriptor, alias);
         if (!known.insert(alias_path).second) {
            throw std::invalid_argument{"duplicate config field alias: " + alias_path};
         }
      }
   }
   components_.push_back(std::move(descriptor));
}

const value* component_view::try_get(std::string_view field) const {
   auto full = section_;
   if (!full.empty()) {
      full += ".";
   }
   full += field;
   return source_->try_get(full);
}

document redact(document input, const component_registry& registry) {
   for (const auto& component : registry.components()) {
      for (const auto& field : component.fields) {
         if (!field.secret) {
            continue;
         }
         auto path = component.section;
         if (!path.empty()) {
            path += ".";
         }
         path += field.name;
         if (input.try_get(path)) {
            input.set(path, std::string{"<redacted>"});
         }
      }
   }
   return input;
}

} // namespace fcl::config
