module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

export module fcl.json;

import fcl.config;
import fcl.schema;
import fcl.variant;

export namespace fcl::json {

enum class unknown_field_policy {
   ignore,
   warn,
   error,
};

struct read_options {
   std::string source_name;
   std::size_t max_depth = 128;
   unknown_field_policy unknown_fields = unknown_field_policy::warn;
};

struct write_options {
   bool pretty = false;
   std::size_t max_bytes = std::numeric_limits<std::size_t>::max();
   std::chrono::system_clock::time_point deadline = std::chrono::system_clock::time_point::max();
};

template <typename T>
struct read_result {
   T value{};
   std::vector<schema::diagnostic> diagnostics;

   [[nodiscard]] bool ok() const {
      return std::ranges::none_of(diagnostics, [](const schema::diagnostic& entry) {
         return entry.level == schema::severity::error;
      });
   }
};

struct write_result {
   std::string text;
   std::vector<schema::diagnostic> diagnostics;

   [[nodiscard]] bool ok() const {
      return std::ranges::none_of(diagnostics, [](const schema::diagnostic& entry) {
         return entry.level == schema::severity::error;
      });
   }
};

[[nodiscard]] read_result<variant> read_value(std::string_view input, read_options options = {});
[[nodiscard]] write_result write_value(const variant& input, write_options options = {});
[[nodiscard]] read_result<config::document> read_document(std::string_view input, read_options options = {});
[[nodiscard]] write_result write_document(const config::document& input, write_options options = {});

[[nodiscard]] read_result<variant> load_value(const std::filesystem::path& path, read_options options = {});
[[nodiscard]] write_result save_value(const std::filesystem::path& path, const variant& input, write_options options = {});
[[nodiscard]] read_result<config::document> load_document(const std::filesystem::path& path, read_options options = {});
[[nodiscard]] write_result save_document(const std::filesystem::path& path, const config::document& input, write_options options = {});

template <typename T>
[[nodiscard]] read_result<T> read(std::string_view input, read_options options = {}) {
   auto output = read_result<T>{};
   const auto rules = schema::rules<T>::define();
   if (!rules.fields().empty()) {
      auto parsed_document = read_document(input, options);
      output.diagnostics = std::move(parsed_document.diagnostics);
      if (!parsed_document.ok()) {
         return output;
      }
      auto decoded = config::decode<T>(parsed_document.value);
      output.value = std::move(decoded.value);
      for (auto entry : std::move(decoded.diagnostics.entries)) {
         if (entry.code == "config.unknown") {
            if (options.unknown_fields == unknown_field_policy::ignore) {
               continue;
            }
            entry.code = "json.unknown";
            if (options.unknown_fields == unknown_field_policy::error) {
               entry.level = schema::severity::error;
            }
         }
         output.diagnostics.push_back(std::move(entry));
      }
      return output;
   }

   auto parsed = read_value(input, options);
   output.diagnostics = std::move(parsed.diagnostics);
   if (!parsed.ok()) {
      return output;
   }

   rules.apply_defaults(output.value);

   if constexpr (requires(const variant& source, T& target) { from_variant(source, target); }) {
      try {
         from_variant(parsed.value, output.value);
      } catch (const std::exception& error) {
         output.diagnostics.push_back(schema::diagnostic{
            .path = {},
            .code = "json.type",
            .level = schema::severity::error,
            .message = error.what(),
         });
         return output;
      }
   } else {
      output.diagnostics.push_back(schema::diagnostic{
         .path = {},
         .code = "json.type",
         .level = schema::severity::error,
         .message = "type is not readable from JSON without schema rules or fcl::from_variant",
      });
      return output;
   }

   if (options.unknown_fields != unknown_field_policy::ignore && parsed.value.is_object()) {
      auto known = std::set<std::string>{};
      for (const auto& field : rules.fields()) {
         known.insert(field.name);
         known.insert(field.aliases.begin(), field.aliases.end());
      }
      if (!known.empty()) {
         for (const auto& entry : parsed.value.get_object()) {
            if (!known.contains(entry.key())) {
               output.diagnostics.push_back(schema::diagnostic{
                  .path = entry.key(),
                  .code = "json.unknown",
                  .level = options.unknown_fields == unknown_field_policy::error ? schema::severity::error : schema::severity::warning,
                  .message = "unknown JSON field",
               });
            }
         }
      }
   }

   auto validation = rules.validate(output.value);
   output.diagnostics.insert(output.diagnostics.end(), validation.begin(), validation.end());
   return output;
}

template <typename T>
[[nodiscard]] read_result<T> load(const std::filesystem::path& path, read_options options = {}) {
   auto parsed = load_value(path, options);
   auto output = read_result<T>{};
   output.diagnostics = std::move(parsed.diagnostics);
   if (!parsed.ok()) {
      return output;
   }

   const auto rules = schema::rules<T>::define();
   if (!rules.fields().empty()) {
      auto parsed_document = load_document(path, options);
      output.diagnostics = std::move(parsed_document.diagnostics);
      if (!parsed_document.ok()) {
         return output;
      }
      auto decoded = config::decode<T>(parsed_document.value);
      output.value = std::move(decoded.value);
      for (auto entry : std::move(decoded.diagnostics.entries)) {
         if (entry.code == "config.unknown") {
            if (options.unknown_fields == unknown_field_policy::ignore) {
               continue;
            }
            entry.code = "json.unknown";
            if (options.unknown_fields == unknown_field_policy::error) {
               entry.level = schema::severity::error;
            }
         }
         output.diagnostics.push_back(std::move(entry));
      }
      return output;
   }

   rules.apply_defaults(output.value);
   if constexpr (requires(const variant& source, T& target) { from_variant(source, target); }) {
      try {
         from_variant(parsed.value, output.value);
      } catch (const std::exception& error) {
         output.diagnostics.push_back(schema::diagnostic{
            .path = {},
            .code = "json.type",
            .level = schema::severity::error,
            .message = error.what(),
         });
         return output;
      }
   } else {
      output.diagnostics.push_back(schema::diagnostic{
         .path = {},
         .code = "json.type",
         .level = schema::severity::error,
         .message = "type is not readable from JSON without schema rules or fcl::from_variant",
      });
      return output;
   }

   auto validation = rules.validate(output.value);
   output.diagnostics.insert(output.diagnostics.end(), validation.begin(), validation.end());
   return output;
}

template <typename T>
[[nodiscard]] write_result write(const T& input, write_options options = {}) {
   return write_value(variant{input}, std::move(options));
}

template <typename T>
[[nodiscard]] write_result save(const std::filesystem::path& path, const T& input, write_options options = {}) {
   return save_value(path, variant{input}, std::move(options));
}

} // namespace fcl::json
