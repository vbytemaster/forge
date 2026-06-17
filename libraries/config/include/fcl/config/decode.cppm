module;

#include <algorithm>
#include <any>
#include <cstdint>
#include <exception>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module fcl.config.decode;

import fcl.config.component;
import fcl.config.document;
import fcl.config.value;
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

export namespace fcl::config {

[[nodiscard]] bool parse_bool_text(std::string text, bool& output);
[[nodiscard]] schema::input_value to_schema_value(const value& input);
[[nodiscard]] std::any value_to_any(const value& input, schema::value_kind kind);
[[nodiscard]] value any_to_value(schema::value_kind kind, const std::any& input);

template <typename T> [[nodiscard]] component_descriptor describe_component(std::string section) {
   auto descriptor = component_descriptor{.section = std::move(section)};
   const auto rules = schema::rules<T>::define();
   for (const auto& field : rules.fields()) {
      descriptor.fields.push_back(field_descriptor{
          .name = field.name,
          .aliases = field.aliases,
          .kind = field.kind,
          .required = field.required,
          .has_default = field.has_default,
          .default_value = field.has_default ? any_to_value(field.kind, field.default_value) : value{},
          .secret = field.secret,
          .deprecated = field.deprecated,
          .deprecated_message = field.deprecated_message,
          .description = field.description,
      });
   }
   return descriptor;
}

struct decode_diagnostics {
   std::vector<schema::diagnostic> entries;

   [[nodiscard]] bool ok() const {
      return std::ranges::none_of(
          entries, [](const schema::diagnostic& entry) { return entry.level == schema::severity::error; });
   }
};

template <typename T> struct decode_result {
   T value{};
   decode_diagnostics diagnostics;

   [[nodiscard]] bool ok() const {
      return diagnostics.ok();
   }
};

template <typename T> [[nodiscard]] decode_result<T> decode(const document& source, std::string_view section = {}) {
   auto result = decode_result<T>{};
   const auto rules = schema::rules<T>::define();
   rules.apply_defaults(result.value);

   auto known_fields = std::set<std::string>{};
   for (const auto& field : rules.fields()) {
      known_fields.insert(field.name);
      known_fields.insert(field.aliases.begin(), field.aliases.end());
   }

   if (const auto* object = source.object_at(section)) {
      for (const auto& [name, ignored] : *object) {
         if (!known_fields.contains(name)) {
            auto full_path = std::string{section};
            if (!full_path.empty()) {
               full_path += ".";
            }
            full_path += name;
            result.diagnostics.entries.push_back(schema::diagnostic{
                .path = std::move(full_path),
                .code = "config.unknown",
                .level = schema::severity::warning,
                .message = "unknown config field",
            });
         }
      }
   }

   for (const auto& field : rules.fields()) {
      auto field_path = std::string{section};
      if (!field_path.empty()) {
         field_path += ".";
      }
      field_path += field.name;

      const auto* found = source.try_get(field_path);
      if (!found) {
         for (const auto& alias : field.aliases) {
            auto alias_path = std::string{section};
            if (!alias_path.empty()) {
               alias_path += ".";
            }
            alias_path += alias;
            found = source.try_get(alias_path);
            if (found) {
               field_path = std::move(alias_path);
               break;
            }
         }
      }

      if (!found) {
         if (field.required) {
            result.diagnostics.entries.push_back(schema::diagnostic{
                .path = std::move(field_path),
                .code = "config.required",
                .level = schema::severity::error,
                .message = "required config field is missing",
            });
         }
         continue;
      }

      if (field.deprecated) {
         result.diagnostics.entries.push_back(schema::diagnostic{
             .path = field_path,
             .code = "config.deprecated",
             .level = schema::severity::warning,
             .message = field.deprecated_message.empty() ? "deprecated config field" : field.deprecated_message,
         });
      }

      field.assign_input(result.value, to_schema_value(*found), field_path, result.diagnostics.entries);
   }

   auto validation = rules.validate(result.value, section);
   result.diagnostics.entries.insert(result.diagnostics.entries.end(), validation.begin(), validation.end());
   return result;
}

template <typename T> [[nodiscard]] document defaults_for(std::string_view section) {
   auto output = document{};
   const auto rules = schema::rules<T>::define();
   for (const auto& field : rules.fields()) {
      if (!field.has_default) {
         continue;
      }
      auto field_path = std::string{section};
      if (!field_path.empty()) {
         field_path += ".";
      }
      field_path += field.name;
      output.set(std::move(field_path), any_to_value(field.kind, field.default_value));
   }
   return output;
}

} // namespace fcl::config
