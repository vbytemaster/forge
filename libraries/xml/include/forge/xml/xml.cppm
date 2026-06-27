module;

#include <algorithm>
#include <any>
#include <boost/describe.hpp>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module forge.xml;

import forge.config.document;
import forge.config.value;
import forge.core.type_name;
import forge.reflect.reflect;
import forge.schema.diagnostic;
import forge.schema.object;
import forge.schema.scalar;
import forge.schema.value_kind;

namespace forge::xml::detail {

template <typename T> struct optional_traits : std::false_type {};
template <typename T> struct optional_traits<std::optional<T>> : std::true_type {
   using value_type = T;
};

template <typename T> struct vector_traits : std::false_type {};
template <typename T, typename Allocator> struct vector_traits<std::vector<T, Allocator>> : std::true_type {
   using value_type = T;
};

template <typename T>
inline constexpr bool described_object_v =
   boost::describe::has_describe_members<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool scalar_value_v =
   std::same_as<std::remove_cvref_t<T>, std::string> || std::same_as<std::remove_cvref_t<T>, bool> ||
   (std::integral<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool>) ||
   std::floating_point<std::remove_cvref_t<T>> || std::is_enum_v<std::remove_cvref_t<T>>;

[[nodiscard]] inline schema::diagnostic make_diagnostic(std::string path,
                                                        std::string code,
                                                        schema::severity level,
                                                        std::string message) {
   return schema::diagnostic{
      .path = std::move(path),
      .code = std::move(code),
      .level = level,
      .message = std::move(message),
   };
}

[[nodiscard]] inline schema::diagnostic make_error(std::string path, std::string code, std::string message) {
   return make_diagnostic(std::move(path), std::move(code), schema::severity::error, std::move(message));
}

[[nodiscard]] inline schema::diagnostic make_warning(std::string path, std::string code, std::string message) {
   return make_diagnostic(std::move(path), std::move(code), schema::severity::warning, std::move(message));
}

[[nodiscard]] inline std::string append_path(std::string_view base, std::string_view field) {
   auto output = std::string{base};
   if (!output.empty()) {
      output += ".";
   }
   output += field;
   return output;
}

[[nodiscard]] inline std::string fallback_root_name(std::string_view type_name) {
   auto name = std::string{type_name};
   if (const auto position = name.rfind("::"); position != std::string::npos) {
      name.erase(0, position + 2);
   }
   if (name.empty()) {
      return "value";
   }

   for (auto& character : name) {
      const auto valid = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                         (character >= '0' && character <= '9') || character == '_' || character == '-' ||
                         character == '.';
      if (!valid) {
         character = '_';
      }
   }
   if (!((name.front() >= 'a' && name.front() <= 'z') || (name.front() >= 'A' && name.front() <= 'Z') ||
         name.front() == '_')) {
      name.insert(name.begin(), '_');
   }
   return name;
}

} // namespace forge::xml::detail

export namespace forge::xml {

enum class unknown_field_policy {
   ignore,
   warn,
   error,
};

struct read_options {
   std::string source_name;
   std::size_t max_bytes = 16U * 1024U * 1024U;
   std::size_t max_depth = 128;
   std::size_t max_attributes = 4096;
   std::size_t max_children = 1U * 1024U * 1024U;
   std::size_t max_text_bytes = 16U * 1024U * 1024U;
   unknown_field_policy unknown_fields = unknown_field_policy::warn;
};

struct write_options {
   std::string root_name;
   std::string default_namespace;
   bool xml_declaration = false;
   bool pretty = false;
   std::size_t max_bytes = std::numeric_limits<std::size_t>::max();
   std::chrono::system_clock::time_point deadline = std::chrono::system_clock::time_point::max();
};

template <typename T> struct read_result {
   T value{};
   std::vector<schema::diagnostic> diagnostics;

   [[nodiscard]] bool ok() const {
      return std::ranges::none_of(
         diagnostics, [](const schema::diagnostic& entry) { return entry.level == schema::severity::error; });
   }
};

struct write_result {
   std::string text;
   std::vector<schema::diagnostic> diagnostics;

   [[nodiscard]] bool ok() const {
      return std::ranges::none_of(
         diagnostics, [](const schema::diagnostic& entry) { return entry.level == schema::severity::error; });
   }
};

struct attribute {
   std::string name;
   std::string value;
};

struct element {
   std::string name;
   std::string text;
   std::vector<attribute> attributes;
   std::vector<element> children;
};

struct document {
   element root;
};

[[nodiscard]] read_result<document> read_value(std::string_view input, read_options options = {});
[[nodiscard]] write_result write_value(const document& input, write_options options = {});

namespace detail {

template <typename Member>
[[nodiscard]] element value_to_element(std::string name,
                                       const Member& value,
                                       std::vector<schema::diagnostic>& diagnostics);

template <typename Member>
void append_member_elements(std::vector<element>& output,
                            std::string_view name,
                            const Member& value,
                            std::vector<schema::diagnostic>& diagnostics);

template <typename T>
void decode_object(const element& input,
                   T& output,
                   std::string_view base_path,
                   const read_options& options,
                   std::vector<schema::diagnostic>& diagnostics);

[[nodiscard]] schema::input_value element_to_input_value(const element& input);
void append_input_value_elements(std::vector<element>& output, std::string name, const schema::input_value& value);

template <typename T>
[[nodiscard]] schema::input_value::object_type root_to_input_object(const element& input,
                                                                    const schema::object_schema<T>& rules,
                                                                    std::vector<schema::diagnostic>& diagnostics,
                                                                    std::string_view base_path = {}) {
   auto output = schema::input_value::object_type{};
   const auto& fields = rules.fields();
   for (const auto& field : fields) {
      auto names = std::set<std::string>{field.name};
      names.insert(field.aliases.begin(), field.aliases.end());

      auto matches = std::vector<const element*>{};
      for (const auto& child : input.children) {
         if (names.contains(child.name)) {
            matches.push_back(&child);
         }
      }

      if (matches.empty()) {
         continue;
      }

      if (field.kind == schema::value_kind::string_list || field.kind == schema::value_kind::object_list) {
         auto array = schema::input_value::array_type{};
         array.reserve(matches.size());
         for (const auto* match : matches) {
            array.push_back(element_to_input_value(*match));
         }
         output.emplace(field.name, schema::input_value{std::move(array)});
      } else {
         if (matches.size() > 1U) {
            diagnostics.push_back(
               make_error(append_path(base_path, field.name), "xml.duplicate", "XML element is repeated for scalar field"));
            continue;
         }
         output.emplace(field.name, element_to_input_value(*matches.front()));
      }
   }
   return output;
}

template <typename T>
void append_schema_object_children(std::vector<element>& output,
                                   const T& input,
                                   std::vector<schema::diagnostic>& diagnostics) {
   const auto rules = schema::rules<T>::define();
   const auto& fields = rules.fields();
   if (!fields.empty()) {
      for (const auto& field : fields) {
         auto matched = false;
         if (!field.member_name.empty()) {
            forge::reflect::for_each_member<T>([&](const char* member_name, auto member) {
               if (matched || field.member_name != member_name) {
                  return;
               }
               append_member_elements(output, field.name, input.*member, diagnostics);
               matched = true;
            });
         }
         if (!matched) {
            auto value = field.read_input(input);
            if (!std::holds_alternative<std::monostate>(value.storage)) {
               append_input_value_elements(output, field.name, value);
            }
         }
      }
      return;
   }

   forge::reflect::for_each_member<T>([&](const char* member_name, auto member) {
      append_member_elements(output, member_name, input.*member, diagnostics);
   });
}

template <typename T>
[[nodiscard]] std::vector<const element*> find_children(const element& input,
                                                        const schema::field_rule<T>* field,
                                                        std::string_view fallback_name) {
   auto names = std::set<std::string>{};
   if (field) {
      names.insert(field->name);
      names.insert(field->aliases.begin(), field->aliases.end());
   } else {
      names.insert(std::string{fallback_name});
   }

   auto output = std::vector<const element*>{};
   for (const auto& child : input.children) {
      if (names.contains(child.name)) {
         output.push_back(&child);
      }
   }
   return output;
}

template <typename T>
void report_unknown_children(const element& input,
                             const std::vector<schema::field_rule<T>>& fields,
                             const read_options& options,
                             std::vector<schema::diagnostic>& diagnostics) {
   if (options.unknown_fields == unknown_field_policy::ignore) {
      return;
   }

   auto known = std::set<std::string>{};
   if (!fields.empty()) {
      for (const auto& field : fields) {
         known.insert(field.name);
         known.insert(field.aliases.begin(), field.aliases.end());
      }
   } else if constexpr (described_object_v<T>) {
      forge::reflect::for_each_member<T>([&](const char* name, auto) { known.insert(name); });
   }

   for (const auto& child : input.children) {
      if (!known.contains(child.name)) {
         diagnostics.push_back(make_diagnostic(child.name,
                                               "xml.unknown",
                                               options.unknown_fields == unknown_field_policy::error
                                                  ? schema::severity::error
                                                  : schema::severity::warning,
                                               "unknown XML child element"));
      }
   }
}

template <typename T>
[[nodiscard]] T decode_scalar_element(const element& input,
                                      std::string_view path,
                                      std::vector<schema::diagnostic>& diagnostics) {
   if (!input.children.empty()) {
      diagnostics.push_back(make_error(std::string{path}, "xml.type", "expected scalar XML text"));
      return {};
   }

   try {
      return schema::parse_scalar_text<T>(input.text);
   } catch (const std::exception& error) {
      diagnostics.push_back(make_error(std::string{path}, "xml.type", error.what()));
      return {};
   }
}

template <typename Member>
[[nodiscard]] Member decode_member_value(const element& input,
                                         std::string_view path,
                                         const read_options& options,
                                         std::vector<schema::diagnostic>& diagnostics) {
   using clean = std::remove_cvref_t<Member>;
   if constexpr (optional_traits<clean>::value) {
      using item_type = typename optional_traits<clean>::value_type;
      return decode_member_value<item_type>(input, path, options, diagnostics);
   } else if constexpr (vector_traits<clean>::value) {
      diagnostics.push_back(make_error(std::string{path},
                                       "xml.type",
                                       "vector XML member must be decoded from repeated child elements"));
      return {};
   } else if constexpr (described_object_v<clean>) {
      auto output = clean{};
      const auto rules = schema::rules<clean>::define();
      rules.apply_defaults(output);
      decode_object(input, output, path, options, diagnostics);
      return output;
   } else {
      return decode_scalar_element<clean>(input, path, diagnostics);
   }
}

template <typename Member, typename T>
void assign_member(T& output,
                   Member T::*member,
                   const std::vector<const element*>& matches,
                   const schema::field_rule<T>* field,
                   std::string_view path,
                   const read_options& options,
                   std::vector<schema::diagnostic>& diagnostics) {
   using clean = std::remove_cvref_t<Member>;
   if constexpr (vector_traits<clean>::value) {
      using item_type = typename vector_traits<clean>::value_type;
      auto values = clean{};
      values.reserve(matches.size());
      for (const auto* child : matches) {
         values.push_back(decode_member_value<item_type>(*child, path, options, diagnostics));
      }
      output.*member = std::move(values);
      return;
   }

   if (matches.empty()) {
      if (field && field->required) {
         diagnostics.push_back(make_error(std::string{path}, "xml.required", "required XML element is missing"));
      }
      return;
   }
   if (matches.size() > 1) {
      diagnostics.push_back(make_error(std::string{path}, "xml.duplicate", "XML element is repeated for scalar field"));
      return;
   }

   if constexpr (optional_traits<clean>::value) {
      using item_type = typename optional_traits<clean>::value_type;
      output.*member = decode_member_value<item_type>(*matches.front(), path, options, diagnostics);
   } else {
      output.*member = decode_member_value<clean>(*matches.front(), path, options, diagnostics);
   }
}

template <typename T>
void decode_object(const element& input,
                   T& output,
                   std::string_view base_path,
                   const read_options& options,
                   std::vector<schema::diagnostic>& diagnostics) {
   const auto rules = schema::rules<T>::define();
   const auto& fields = rules.fields();
   report_unknown_children<T>(input, fields, options, diagnostics);

   auto index = std::size_t{0};
   forge::reflect::for_each_member<T>([&](const char* member_name, auto member) {
      const auto* field = index < fields.size() ? &fields[index] : nullptr;
      const auto name = field ? std::string_view{field->name} : std::string_view{member_name};
      const auto path = append_path(base_path, name);
      const auto matches = find_children(input, field, member_name);
      assign_member(output, member, matches, field, path, options, diagnostics);
      ++index;
   });
}

template <typename Member>
void append_member_elements(std::vector<element>& output,
                            std::string_view name,
                            const Member& value,
                            std::vector<schema::diagnostic>& diagnostics) {
   using clean = std::remove_cvref_t<Member>;
   if constexpr (optional_traits<clean>::value) {
      if (value.has_value()) {
         append_member_elements(output, name, *value, diagnostics);
      }
   } else if constexpr (vector_traits<clean>::value) {
      for (const auto& item : value) {
         output.push_back(value_to_element(std::string{name}, item, diagnostics));
      }
   } else {
      output.push_back(value_to_element(std::string{name}, value, diagnostics));
   }
}

template <typename T>
[[nodiscard]] element object_to_element(std::string name,
                                        const T& value,
                                        std::vector<schema::diagnostic>& diagnostics) {
   auto output = element{.name = std::move(name)};
   const auto rules = schema::rules<T>::define();
   const auto& fields = rules.fields();
   if (!fields.empty()) {
      append_schema_object_children(output.children, value, diagnostics);
   } else {
      forge::reflect::for_each_member<T>([&](const char* member_name, auto member) {
         append_member_elements(output.children, member_name, value.*member, diagnostics);
      });
   }
   return output;
}

template <typename Member>
[[nodiscard]] element value_to_element(std::string name,
                                       const Member& value,
                                       std::vector<schema::diagnostic>& diagnostics) {
   using clean = std::remove_cvref_t<Member>;
   if constexpr (described_object_v<clean>) {
      return object_to_element(std::move(name), value, diagnostics);
   } else {
      auto output = element{.name = std::move(name)};
      const auto text = schema::format_scalar_text(value);
      if (!text) {
         diagnostics.push_back(make_error(output.name, "xml.type", "field is not representable as XML text"));
         return output;
      }
      output.text = *text;
      return output;
   }
}

} // namespace detail

template <typename T> [[nodiscard]] read_result<T> read(std::string_view input, read_options options = {}) {
   auto output = read_result<T>{};
   auto parsed = read_value(input, options);
   output.diagnostics = std::move(parsed.diagnostics);
   if (!parsed.ok()) {
      return output;
   }

   if constexpr (detail::scalar_value_v<T>) {
      output.value = detail::decode_scalar_element<T>(parsed.value.root, {}, output.diagnostics);
   } else {
      auto rules = schema::rules<T>::define();
      rules.apply_defaults(output.value);
      if (!rules.fields().empty()) {
         detail::report_unknown_children<T>(parsed.value.root, rules.fields(), options, output.diagnostics);
         auto decoded = rules.decode_object(detail::root_to_input_object(parsed.value.root, rules, output.diagnostics),
                                            {},
                                            output.value);
         for (auto entry : std::move(decoded)) {
            if (entry.code == "config.unknown") {
               if (options.unknown_fields == unknown_field_policy::ignore) {
                  continue;
               }
               entry.code = "xml.unknown";
               if (options.unknown_fields == unknown_field_policy::error) {
                  entry.level = schema::severity::error;
               }
            }
            output.diagnostics.push_back(std::move(entry));
         }
      } else if constexpr (detail::described_object_v<T>) {
         detail::decode_object(parsed.value.root, output.value, {}, options, output.diagnostics);
      } else {
         output.diagnostics.push_back(detail::make_error({}, "xml.type", "type is not readable from XML"));
         return output;
      }
      auto validation = rules.validate(output.value);
      output.diagnostics.insert(output.diagnostics.end(), validation.begin(), validation.end());
   }
   return output;
}

template <typename T> [[nodiscard]] write_result write(const T& input, write_options options = {}) {
   auto result = write_result{};
   if constexpr (!detail::scalar_value_v<T>) {
      const auto rules = schema::rules<T>::define();
      auto validation = rules.validate(input);
      result.diagnostics.insert(result.diagnostics.end(), validation.begin(), validation.end());
      if (!result.ok()) {
         return result;
      }

      const auto root_name = !options.root_name.empty()
                                ? options.root_name
                                : detail::fallback_root_name(forge::type_name<std::remove_cvref_t<T>>());
      auto doc = document{.root = element{.name = root_name}};
      if (!rules.fields().empty()) {
         detail::append_schema_object_children(doc.root.children, input, result.diagnostics);
      } else if constexpr (detail::described_object_v<T>) {
         doc.root = detail::object_to_element(root_name, input, result.diagnostics);
      } else {
         result.diagnostics.push_back(detail::make_error({}, "xml.type", "type is not writable to XML"));
         return result;
      }
      if (!result.ok()) {
         return result;
      }
      return write_value(doc, std::move(options));
   } else {
      const auto root_name = !options.root_name.empty() ? options.root_name : std::string{"value"};
      auto doc = document{.root = detail::value_to_element(root_name, input, result.diagnostics)};
      if (!result.ok()) {
         return result;
      }
      return write_value(doc, std::move(options));
   }
}

} // namespace forge::xml
