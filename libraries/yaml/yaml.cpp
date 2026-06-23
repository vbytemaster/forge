module;

#include <glaze/glaze.hpp>
#include <glaze/yaml/read.hpp>
#include <glaze/yaml/write.hpp>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module forge.yaml;

import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

namespace forge::yaml {
namespace {

using codec_value = glz::generic_json<glz::num_mode::u64>;

constexpr glz::yaml::yaml_opts yaml_read_options{
    .format = glz::YAML,
    .error_on_unknown_keys = false,
    .error_on_missing_keys = false,
    .skip_null_members = false,
    .indent_width = 2,
    .flow_style = false,
};

constexpr glz::yaml::yaml_opts yaml_write_block_options{
    .format = glz::YAML,
    .error_on_unknown_keys = false,
    .error_on_missing_keys = false,
    .skip_null_members = false,
    .indent_width = 2,
    .flow_style = false,
};

constexpr glz::yaml::yaml_opts yaml_write_flow_options{
    .format = glz::YAML,
    .error_on_unknown_keys = false,
    .error_on_missing_keys = false,
    .skip_null_members = false,
    .indent_width = 2,
    .flow_style = true,
};

template <class... Ts> struct overloaded : Ts... {
   using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

[[nodiscard]] schema::diagnostic make_error(std::string path, std::string code, std::string message) {
   return schema::diagnostic{
       .path = std::move(path),
       .code = std::move(code),
       .level = schema::severity::error,
       .message = std::move(message),
   };
}

[[nodiscard]] variant to_variant_value(const codec_value& input);
[[nodiscard]] config::value to_config_value(const codec_value& input);
[[nodiscard]] codec_value from_variant_value(const variant& input);
[[nodiscard]] codec_value from_config_value(const config::value& input);

[[nodiscard]] variant to_variant_value(const codec_value& input) {
   return std::visit(overloaded{
                         [](std::nullptr_t) -> variant { return variant{}; },
                         [](std::uint64_t value) -> variant { return variant{value}; },
                         [](std::int64_t value) -> variant { return variant{value}; },
                         [](double value) -> variant { return variant{value}; },
                         [](const std::string& value) -> variant { return variant{value}; },
                         [](bool value) -> variant { return variant{value}; },
                         [](const codec_value::array_t& value) -> variant {
                            auto array = variants{};
                            array.reserve(value.size());
                            for (const auto& entry : value) {
                               array.push_back(to_variant_value(entry));
                            }
                            return variant{std::move(array)};
                         },
                         [](const codec_value::object_t& value) -> variant {
                            auto object = mutable_variant_object{};
                            for (const auto& [key, entry] : value) {
                               object.set(key, to_variant_value(entry));
                            }
                            return variant{std::move(object)};
                         },
                     },
                     input.data);
}

[[nodiscard]] config::value to_config_value(const codec_value& input) {
   return std::visit(overloaded{
                         [](std::nullptr_t) -> config::value { return config::value{}; },
                         [](std::uint64_t value) -> config::value { return config::value{value}; },
                         [](std::int64_t value) -> config::value { return config::value{value}; },
                         [](double value) -> config::value { return config::value{value}; },
                         [](const std::string& value) -> config::value { return config::value{value}; },
                         [](bool value) -> config::value { return config::value{value}; },
                         [](const codec_value::array_t& value) -> config::value {
                            auto array = config::value::array_type{};
                            array.reserve(value.size());
                            for (const auto& entry : value) {
                               array.push_back(to_config_value(entry));
                            }
                            return config::value{std::move(array)};
                         },
                         [](const codec_value::object_t& value) -> config::value {
                            auto object = config::value::object_type{};
                            for (const auto& [key, entry] : value) {
                               object.emplace(key, to_config_value(entry));
                            }
                            return config::value{std::move(object)};
                         },
                     },
                     input.data);
}

[[nodiscard]] codec_value from_variant_value(const variant& input) {
   auto output = codec_value{};
   switch (input.get_type()) {
   case variant::null_type:
      output = nullptr;
      break;
   case variant::int64_type:
      output = input.as_int64();
      break;
   case variant::uint64_type:
      output = input.as_uint64();
      break;
   case variant::double_type:
      output = input.as_double();
      break;
   case variant::bool_type:
      output = input.as_bool();
      break;
   case variant::string_type:
      output = input.get_string();
      break;
   case variant::array_type: {
      auto array = codec_value::array_t{};
      array.reserve(input.get_array().size());
      for (const auto& entry : input.get_array()) {
         array.push_back(from_variant_value(entry));
      }
      output = std::move(array);
      break;
   }
   case variant::object_type: {
      auto object = codec_value::object_t{};
      for (const auto& entry : input.get_object()) {
         object.insert(std::make_pair(entry.key(), from_variant_value(entry.value())));
      }
      output = std::move(object);
      break;
   }
   case variant::blob_type:
      output = input.as_string();
      break;
   }
   return output;
}

[[nodiscard]] codec_value from_config_value(const config::value& input) {
   auto output = codec_value{};
   std::visit(overloaded{
                  [&](std::monostate) { output = nullptr; },
                  [&](bool value) { output = value; },
                  [&](std::int64_t value) { output = value; },
                  [&](std::uint64_t value) { output = value; },
                  [&](double value) { output = value; },
                  [&](const std::string& value) { output = value; },
                  [&](const config::value::array_type& value) {
                     auto array = codec_value::array_t{};
                     array.reserve(value.size());
                     for (const auto& entry : value) {
                        array.push_back(from_config_value(entry));
                     }
                     output = std::move(array);
                  },
                  [&](const config::value::object_type& value) {
                     auto object = codec_value::object_t{};
                     for (const auto& [key, entry] : value) {
                        object.insert(std::make_pair(key, from_config_value(entry)));
                     }
                     output = std::move(object);
                  },
              },
              input.storage);
   return output;
}

[[nodiscard]] bool exceeds_depth(const variant& value, std::size_t max_depth, std::size_t depth = 0) {
   if (depth > max_depth) {
      return true;
   }
   if (value.is_array()) {
      for (const auto& entry : value.get_array()) {
         if (exceeds_depth(entry, max_depth, depth + 1)) {
            return true;
         }
      }
   } else if (value.is_object()) {
      for (const auto& entry : value.get_object()) {
         if (exceeds_depth(entry.value(), max_depth, depth + 1)) {
            return true;
         }
      }
   }
   return false;
}

[[nodiscard]] bool exceeds_depth(const config::value& value, std::size_t max_depth, std::size_t depth = 0) {
   if (depth > max_depth) {
      return true;
   }
   if (const auto* array = value.as_array()) {
      for (const auto& entry : *array) {
         if (exceeds_depth(entry, max_depth, depth + 1)) {
            return true;
         }
      }
   } else if (const auto* object = value.as_object()) {
      for (const auto& [unused, entry] : *object) {
         if (exceeds_depth(entry, max_depth, depth + 1)) {
            return true;
         }
      }
   }
   return false;
}

[[nodiscard]] read_result<codec_value> read_codec_value(std::string_view input, const read_options& options) {
   auto result = read_result<codec_value>{};
   auto parsed = codec_value{};
   if (auto error = glz::read<yaml_read_options>(parsed, input)) {
      result.diagnostics.push_back(make_error(options.source_name, "yaml.parse", glz::format_error(error, input)));
      return result;
   }
   result.value = std::move(parsed);
   return result;
}

[[nodiscard]] write_result write_codec_value(const codec_value& input, const write_options& options) {
   auto result = write_result{};
   if (std::chrono::system_clock::now() > options.deadline) {
      result.diagnostics.push_back(make_error({}, "yaml.deadline", "YAML write deadline expired"));
      return result;
   }

   auto text = std::string{};
   const auto error = options.flow_style ? glz::write<yaml_write_flow_options>(input, text)
                                         : glz::write<yaml_write_block_options>(input, text);
   if (error) {
      result.diagnostics.push_back(make_error({}, "yaml.write", glz::format_error(error, text)));
      return result;
   }
   if (text.size() > options.max_bytes) {
      result.diagnostics.push_back(make_error({}, "yaml.max-bytes", "YAML output exceeds configured byte limit"));
      return result;
   }
   result.text = std::move(text);
   return result;
}

[[nodiscard]] std::string read_file_text(const std::filesystem::path& path,
                                         std::vector<schema::diagnostic>& diagnostics, std::string_view code) {
   auto input = std::ifstream{path, std::ios::binary};
   if (!input) {
      diagnostics.push_back(make_error(path.string(), std::string{code}, "failed to open file"));
      return {};
   }
   return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool write_file_text(const std::filesystem::path& path, std::string_view text,
                                   std::vector<schema::diagnostic>& diagnostics, std::string_view code) {
   auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
   if (!output) {
      diagnostics.push_back(make_error(path.string(), std::string{code}, "failed to open file for writing"));
      return false;
   }
   output.write(text.data(), static_cast<std::streamsize>(text.size()));
   if (!output) {
      diagnostics.push_back(make_error(path.string(), std::string{code}, "failed to write file"));
      return false;
   }
   return true;
}

} // namespace

read_result<variant> read_value(std::string_view input, read_options options) {
   auto parsed = read_codec_value(input, options);
   auto result = read_result<variant>{};
   result.diagnostics = std::move(parsed.diagnostics);
   if (!parsed.ok()) {
      return result;
   }
   result.value = to_variant_value(parsed.value);
   if (exceeds_depth(result.value, options.max_depth)) {
      result.diagnostics.push_back(
          make_error(options.source_name, "yaml.depth", "YAML input exceeds configured maximum depth"));
   }
   return result;
}

write_result write_value(const variant& input, write_options options) {
   return write_codec_value(from_variant_value(input), options);
}

read_result<config::document> read_document(std::string_view input, read_options options) {
   auto parsed = read_codec_value(input, options);
   auto result = read_result<config::document>{};
   result.diagnostics = std::move(parsed.diagnostics);
   if (!parsed.ok()) {
      return result;
   }

   auto root = to_config_value(parsed.value);
   if (exceeds_depth(root, options.max_depth)) {
      result.diagnostics.push_back(
          make_error(options.source_name, "yaml.depth", "YAML input exceeds configured maximum depth"));
      return result;
   }
   const auto* object = root.as_object();
   if (!object) {
      result.diagnostics.push_back(
          make_error(options.source_name, "yaml.document", "YAML config document root must be an object"));
      return result;
   }
   result.value.root = *object;
   return result;
}

write_result write_document(const config::document& input, write_options options) {
   return write_codec_value(from_config_value(config::value{input.root}), options);
}

read_result<variant> load_value(const std::filesystem::path& path, read_options options) {
   auto diagnostics = std::vector<schema::diagnostic>{};
   const auto text = read_file_text(path, diagnostics, "yaml.io");
   if (!diagnostics.empty()) {
      return read_result<variant>{.diagnostics = std::move(diagnostics)};
   }
   if (options.source_name.empty()) {
      options.source_name = path.string();
   }
   return read_value(text, std::move(options));
}

write_result save_value(const std::filesystem::path& path, const variant& input, write_options options) {
   auto result = write_value(input, std::move(options));
   if (!result.ok()) {
      return result;
   }
   if (!write_file_text(path, result.text, result.diagnostics, "yaml.io")) {
      return result;
   }
   return result;
}

read_result<config::document> load_document(const std::filesystem::path& path, read_options options) {
   auto diagnostics = std::vector<schema::diagnostic>{};
   const auto text = read_file_text(path, diagnostics, "yaml.io");
   if (!diagnostics.empty()) {
      return read_result<config::document>{.diagnostics = std::move(diagnostics)};
   }
   if (options.source_name.empty()) {
      options.source_name = path.string();
   }
   return read_document(text, std::move(options));
}

write_result save_document(const std::filesystem::path& path, const config::document& input, write_options options) {
   auto result = write_document(input, std::move(options));
   if (!result.ok()) {
      return result;
   }
   if (!write_file_text(path, result.text, result.diagnostics, "yaml.io")) {
      return result;
   }
   return result;
}

} // namespace forge::yaml
