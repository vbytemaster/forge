module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module fcl.plugins.signature_provider.plugin;

import fcl.config.component;
import fcl.config.value;
import fcl.crypto.asymmetric;
import fcl.exceptions;
import fcl.plugins.signature_provider.exceptions;
import fcl.plugins.signature_provider.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"

namespace fcl::plugins::signature_provider {
namespace {

[[nodiscard]] std::string require_string(const fcl::config::value::object_type& object, std::string_view key,
                                         std::string fallback = {}) {
   const auto found = object.find(std::string{key});
   if (found == object.end()) {
      return fallback;
   }
   const auto* value = std::get_if<std::string>(&found->second.storage);
   if (value == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature provider config field must be a string",
                          fcl::exceptions::ctx("field", std::string{key}));
   }
   return *value;
}

[[nodiscard]] std::vector<std::string> optional_string_list(const fcl::config::value::object_type& object,
                                                           std::string_view key) {
   const auto found = object.find(std::string{key});
   if (found == object.end()) {
      return {};
   }
   const auto* values = std::get_if<fcl::config::value::array_type>(&found->second.storage);
   if (values == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature provider config field must be a string list",
                          fcl::exceptions::ctx("field", std::string{key}));
   }
   auto result = std::vector<std::string>{};
   result.reserve(values->size());
   for (const auto& item : *values) {
      const auto* text = std::get_if<std::string>(&item.storage);
      if (text == nullptr) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config,
                             "signature provider config list item must be a string",
                             fcl::exceptions::ctx("field", std::string{key}));
      }
      result.push_back(*text);
   }
   return result;
}

} // namespace

config decode_config(const fcl::config::component_view& view) {
   auto result = config{};
   result.default_output_profile = view.get_or<std::string>("default-output-profile", "fcl");

   const auto* keys_value = view.try_get("keys");
   if (keys_value == nullptr) {
      return result;
   }
   const auto* keys = std::get_if<fcl::config::value::array_type>(&keys_value->storage);
   if (keys == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature-provider keys must be an array");
   }

   auto ids = std::set<std::string>{};
   result.keys.reserve(keys->size());
   for (const auto& item : *keys) {
      const auto* object = std::get_if<fcl::config::value::object_type>(&item.storage);
      if (object == nullptr) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature-provider key entry must be an object");
      }
      auto entry = key{
         .id = require_string(*object, "id"),
         .private_key = require_string(*object, "private-key"),
         .input_profile = require_string(*object, "input-profile", "fcl"),
         .purposes = optional_string_list(*object, "purposes"),
      };
      if (entry.id.empty()) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature-provider key id is required");
      }
      if (entry.private_key.empty()) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature-provider private-key is required");
      }
      if (entry.purposes.empty()) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config,
                             "signature-provider key purposes must be explicitly configured",
                             fcl::exceptions::ctx("key_id", entry.id));
      }
      if (std::ranges::any_of(entry.purposes, [](const auto& purpose) {
             return purpose.empty();
          })) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config,
                             "signature-provider key purpose must not be empty",
                             fcl::exceptions::ctx("key_id", entry.id));
      }
      if (!ids.insert(entry.id).second) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature-provider key id is duplicated",
                             fcl::exceptions::ctx("key_id", entry.id));
      }
      result.keys.push_back(std::move(entry));
   }
   return result;
}

void apply_config(plugin::impl& state, fcl::config::component_view view) {
   auto config = decode_config(view);
   (void)state.profile_by_name(config.default_output_profile);
   auto loaded = std::map<std::string, plugin::impl::loaded_key>{};
   for (auto& key : config.keys) {
      const auto& input_profile = state.profile_by_name(key.input_profile);
      auto private_key = fcl::crypto::asymmetric::private_key{};
      try {
         private_key = input_profile.parse_private(key.private_key);
      } catch (const std::exception&) {
         FCL_THROW_EXCEPTION(exceptions::invalid_key, "signature provider private key is invalid",
                             fcl::exceptions::ctx("key_id", key.id),
                             fcl::exceptions::ctx("input_profile", key.input_profile));
      }
      loaded.emplace(key.id, plugin::impl::loaded_key{
                                .key_id = key.id,
                                .private_key = std::move(private_key),
                                .purposes = std::move(key.purposes),
                             });
   }
   state.keys = std::move(loaded);
   state.default_output_profile = std::move(config.default_output_profile);
   state.stopping = false;
}

} // namespace fcl::plugins::signature_provider
