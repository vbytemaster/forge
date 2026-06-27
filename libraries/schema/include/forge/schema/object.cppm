module;

#include <any>
#include <algorithm>
#include <boost/describe.hpp>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <cctype>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <typeindex>
#include <variant>
#include <utility>
#include <vector>

export module forge.schema.object;

import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.enums;
import forge.schema.scalar;

export namespace forge::schema {

template <typename T> struct rules;
template <typename T> struct member_pointer_traits;

template <typename Object, typename Member> struct member_pointer_traits<Member Object::*> {
   using object_type = Object;
   using member_type = Member;
};

struct input_value {
   using array_type = std::vector<input_value>;
   using object_type = std::map<std::string, input_value>;
   using storage_type =
      std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, array_type, object_type>;

   storage_type storage;

   input_value() = default;
   input_value(bool input) : storage{input} {}
   input_value(std::int64_t input) : storage{input} {}
   input_value(std::uint64_t input) : storage{input} {}
   input_value(double input) : storage{input} {}
   input_value(std::string input) : storage{std::move(input)} {}
   input_value(array_type input) : storage{std::move(input)} {}
   input_value(object_type input) : storage{std::move(input)} {}

   [[nodiscard]] const array_type* as_array() const noexcept {
      return std::get_if<array_type>(&storage);
   }

   [[nodiscard]] const object_type* as_object() const noexcept {
      return std::get_if<object_type>(&storage);
   }
};

template <typename T> [[nodiscard]] T cast_any_to(const std::any& value) {
   using clean_type = std::remove_cvref_t<T>;
   if (value.type() == typeid(clean_type)) {
      return std::any_cast<clean_type>(value);
   }
   if constexpr (std::same_as<clean_type, std::string>) {
      if (value.type() == typeid(const char*)) {
         return std::string{std::any_cast<const char*>(value)};
      }
      if (value.type() == typeid(char*)) {
         return std::string{std::any_cast<char*>(value)};
      }
   } else if constexpr (std::integral<clean_type> && !std::same_as<clean_type, bool>) {
      if (value.type() == typeid(int)) {
         return checked_integral_cast<clean_type>(std::any_cast<int>(value));
      }
      if (value.type() == typeid(unsigned int)) {
         return checked_integral_cast<clean_type>(std::any_cast<unsigned int>(value));
      }
      if (value.type() == typeid(long)) {
         return checked_integral_cast<clean_type>(std::any_cast<long>(value));
      }
      if (value.type() == typeid(unsigned long)) {
         return checked_integral_cast<clean_type>(std::any_cast<unsigned long>(value));
      }
      if (value.type() == typeid(long long)) {
         return checked_integral_cast<clean_type>(std::any_cast<long long>(value));
      }
      if (value.type() == typeid(unsigned long long)) {
         return checked_integral_cast<clean_type>(std::any_cast<unsigned long long>(value));
      }
      if (value.type() == typeid(std::int64_t)) {
         return checked_integral_cast<clean_type>(std::any_cast<std::int64_t>(value));
      }
      if (value.type() == typeid(std::uint64_t)) {
         return checked_integral_cast<clean_type>(std::any_cast<std::uint64_t>(value));
      }
   } else if constexpr (std::floating_point<clean_type>) {
      if (value.type() == typeid(float)) {
         return static_cast<T>(std::any_cast<float>(value));
      }
      if (value.type() == typeid(double)) {
         return static_cast<T>(std::any_cast<double>(value));
      }
      if (value.type() == typeid(long double)) {
         return static_cast<T>(std::any_cast<long double>(value));
      }
   } else if constexpr (std::is_enum_v<clean_type>) {
      if (value.type() == typeid(std::string)) {
         auto parsed = clean_type{};
         if (enum_from_config_string(std::any_cast<std::string>(value), parsed)) {
            return parsed;
         }
      }
      if (value.type() == typeid(const char*)) {
         auto parsed = clean_type{};
         if (enum_from_config_string(std::any_cast<const char*>(value), parsed)) {
            return parsed;
         }
      }
      if (value.type() == typeid(char*)) {
         auto parsed = clean_type{};
         if (enum_from_config_string(std::any_cast<char*>(value), parsed)) {
            return parsed;
         }
      }
      if (value.type() == typeid(int)) {
         auto parsed = clean_type{};
         if (enum_from_int(static_cast<std::int64_t>(std::any_cast<int>(value)), parsed)) {
            return parsed;
         }
      }
      if (value.type() == typeid(std::int64_t)) {
         auto parsed = clean_type{};
         if (enum_from_int(std::any_cast<std::int64_t>(value), parsed)) {
            return parsed;
         }
      }
   }
   return std::any_cast<clean_type>(value);
}

[[nodiscard]] inline std::string append_path(std::string_view base_path, std::string_view field) {
   auto output = std::string{base_path};
   if (!output.empty()) {
      output += ".";
   }
   output += field;
   return output;
}

[[nodiscard]] inline std::string append_index(std::string_view base_path, std::size_t index) {
   auto output = std::string{base_path};
   output += "[";
   output += std::to_string(index);
   output += "]";
   return output;
}

[[nodiscard]] inline diagnostic make_path_error(std::string path, std::string code, std::string message) {
   return diagnostic{
      .path = std::move(path),
      .code = std::move(code),
      .level = severity::error,
      .message = std::move(message),
   };
}

[[nodiscard]] inline diagnostic make_path_warning(std::string path, std::string code, std::string message) {
   return diagnostic{
      .path = std::move(path),
      .code = std::move(code),
      .level = severity::warning,
      .message = std::move(message),
   };
}

template <typename T>
[[nodiscard]] T cast_input_to(const input_value& input, std::string_view path, std::vector<diagnostic>& diagnostics);

template <typename T>
[[nodiscard]] input_value to_input_value(const T& input);

template <typename T>
[[nodiscard]] std::vector<T> decode_object_list(const input_value& input,
                                                std::string_view path,
                                                std::vector<diagnostic>& diagnostics);

template <typename T> struct field_rule {
   std::string name;
   std::vector<std::string> aliases;
   value_kind kind = value_kind::string;
   std::type_index type = std::type_index{typeid(void)};
   bool required = false;
   bool secret = false;
   bool deprecated = false;
   std::string deprecated_message;
   std::string description;
   bool has_default = false;
   std::any default_value;
   std::optional<long double> minimum;
   std::optional<long double> maximum;
   bool nested_object_list = false;
   std::type_index item_type = std::type_index{typeid(void)};
   std::function<void(T&)> apply_default;
   std::function<void(T&, const std::any&)> assign_any;
   std::function<void(T&, const input_value&, std::string_view, std::vector<diagnostic>&)> assign_input;
   std::function<std::any(const T&)> read_any;
   std::function<input_value(const T&)> read_input;
   std::function<std::optional<std::size_t>(const T&)> read_size;
   std::vector<std::function<void(const T&, std::string_view, std::vector<diagnostic>&)>> validators;
};

template <typename T> class field_builder;

template <typename T> class object_schema {
 public:
   object_schema() : fields_{std::make_shared<std::vector<field_rule<T>>>()} {}

   template <auto Member> field_builder<T> field(std::string name) {
      using pointer_traits = member_pointer_traits<decltype(Member)>;
      using object_type = typename pointer_traits::object_type;
      using member_type = std::remove_cvref_t<typename pointer_traits::member_type>;
      static_assert(std::same_as<object_type, T>, "schema field member must belong to schema object type");

      auto rule = field_rule<T>{};
      rule.name = std::move(name);
      rule.kind = member_kind<member_type>::value;
      rule.type = std::type_index{typeid(member_type)};
      rule.assign_any = [](T& object, const std::any& value) { object.*Member = cast_any_to<member_type>(value); };
      rule.assign_input = [](T& object, const input_value& value, std::string_view path,
                             std::vector<diagnostic>& diagnostics) {
         try {
            object.*Member = cast_input_to<member_type>(value, path, diagnostics);
         } catch (const std::exception& error) {
            diagnostics.push_back(make_path_error(std::string{path}, "config.type", error.what()));
         }
      };
      rule.read_any = [](const T& object) -> std::any { return object.*Member; };
      rule.read_input = [](const T& object) -> input_value { return to_input_value(object.*Member); };
      if constexpr (is_vector<member_type>::value) {
         rule.read_size = [](const T& object) -> std::optional<std::size_t> { return (object.*Member).size(); };
      }
      rule.apply_default = [state = fields_, index = fields_->size()](T& object) {
         const auto& self = (*state)[index];
         if (self.has_default) {
            self.assign_any(object, self.default_value);
         }
      };

      fields_->push_back(std::move(rule));
      return field_builder<T>{*this, fields_->size() - 1};
   }

   [[nodiscard]] const std::vector<field_rule<T>>& fields() const noexcept {
      return *fields_;
   }

   void apply_defaults(T& object) const {
      for (const auto& field : *fields_) {
         field.apply_default(object);
      }
   }

   [[nodiscard]] std::vector<diagnostic> decode_object(const input_value::object_type& input,
                                                       std::string_view base_path,
                                                       T& output) const {
      auto result = std::vector<diagnostic>{};
      auto known_fields = std::set<std::string>{};
      for (const auto& field : *fields_) {
         known_fields.insert(field.name);
         known_fields.insert(field.aliases.begin(), field.aliases.end());
      }

      for (const auto& [name, ignored] : input) {
         if (!known_fields.contains(name)) {
            result.push_back(make_path_warning(append_path(base_path, name), "config.unknown", "unknown config field"));
         }
      }

      for (const auto& field : *fields_) {
         auto field_path = append_path(base_path, field.name);
         const auto* found = [&]() -> const input_value* {
            if (const auto exact = input.find(field.name); exact != input.end()) {
               return &exact->second;
            }
            for (const auto& alias : field.aliases) {
               if (const auto alias_entry = input.find(alias); alias_entry != input.end()) {
                  field_path = append_path(base_path, alias);
                  return &alias_entry->second;
               }
            }
            return nullptr;
         }();

         if (!found) {
            if (field.required) {
               result.push_back(
                  make_path_error(std::move(field_path), "config.required", "required config field is missing"));
            }
            continue;
         }

         if (field.deprecated) {
            result.push_back(make_path_warning(
               field_path,
               "config.deprecated",
               field.deprecated_message.empty() ? "deprecated config field" : field.deprecated_message));
         }

         field.assign_input(output, *found, field_path, result);
      }

      auto validation = validate(output, base_path);
      result.insert(result.end(), validation.begin(), validation.end());
      return result;
   }

   [[nodiscard]] std::vector<diagnostic> validate(const T& object, std::string_view base_path = {}) const {
      auto result = std::vector<diagnostic>{};
      for (const auto& field : *fields_) {
         if (field.minimum || field.maximum) {
            auto numeric = std::optional<long double>{};
            try {
               switch (field.kind) {
               case value_kind::signed_integer:
                  numeric =
                      static_cast<long double>(std::any_cast<std::int64_t>(coerce_signed(field.read_any(object))));
                  break;
               case value_kind::unsigned_integer:
                  numeric =
                      static_cast<long double>(std::any_cast<std::uint64_t>(coerce_unsigned(field.read_any(object))));
                  break;
               case value_kind::floating:
                  numeric = std::any_cast<long double>(coerce_floating(field.read_any(object)));
                  break;
               default:
                  break;
               }
            } catch (...) {
               result.push_back(
                   make_error(base_path, field.name, "schema.type", "value cannot be inspected for range validation"));
            }

            if (numeric && field.minimum && *numeric < *field.minimum) {
               result.push_back(make_error(base_path, field.name, "schema.range", "value is below the allowed minimum"));
            }
            if (numeric && field.maximum && *numeric > *field.maximum) {
               result.push_back(make_error(base_path, field.name, "schema.range", "value is above the allowed maximum"));
            }
         }
         for (const auto& validator : field.validators) {
            validator(object, base_path, result);
         }
      }
      return result;
   }

 private:
   friend class field_builder<T>;

   field_rule<T>& field_at(std::size_t index) {
      return (*fields_)[index];
   }

   static std::any coerce_signed(const std::any& value) {
      if (value.type() == typeid(std::int64_t)) {
         return value;
      }
      if (value.type() == typeid(int)) {
         return static_cast<std::int64_t>(std::any_cast<int>(value));
      }
      if (value.type() == typeid(short)) {
         return static_cast<std::int64_t>(std::any_cast<short>(value));
      }
      if (value.type() == typeid(long)) {
         return static_cast<std::int64_t>(std::any_cast<long>(value));
      }
      if (value.type() == typeid(long long)) {
         return static_cast<std::int64_t>(std::any_cast<long long>(value));
      }
      return value;
   }

   static std::any coerce_unsigned(const std::any& value) {
      if (value.type() == typeid(std::uint64_t)) {
         return value;
      }
      if (value.type() == typeid(unsigned int)) {
         return static_cast<std::uint64_t>(std::any_cast<unsigned int>(value));
      }
      if (value.type() == typeid(unsigned short)) {
         return static_cast<std::uint64_t>(std::any_cast<unsigned short>(value));
      }
      if (value.type() == typeid(unsigned long)) {
         return static_cast<std::uint64_t>(std::any_cast<unsigned long>(value));
      }
      if (value.type() == typeid(unsigned long long)) {
         return static_cast<std::uint64_t>(std::any_cast<unsigned long long>(value));
      }
      return value;
   }

   static std::any coerce_floating(const std::any& value) {
      if (value.type() == typeid(long double)) {
         return value;
      }
      if (value.type() == typeid(double)) {
         return static_cast<long double>(std::any_cast<double>(value));
      }
      if (value.type() == typeid(float)) {
         return static_cast<long double>(std::any_cast<float>(value));
      }
      return value;
   }

   static diagnostic make_error(std::string_view base_path, const std::string& field, std::string code,
                                std::string message) {
      auto path = std::string{base_path};
      if (!path.empty()) {
         path += ".";
      }
      path += field;
      return diagnostic{
          .path = std::move(path), .code = std::move(code), .level = severity::error, .message = std::move(message)};
   }

   std::shared_ptr<std::vector<field_rule<T>>> fields_;
};

template <typename T> class field_builder {
 public:
   field_builder(object_schema<T> schema, std::size_t index) : schema_{std::move(schema)}, index_{index} {}

   field_builder& required() {
      current().required = true;
      return *this;
   }

   template <typename Value> field_builder& default_value(Value&& value) {
      current().has_default = true;
      current().default_value = std::forward<Value>(value);
      return *this;
   }

   template <typename Min, typename Max> field_builder& range(Min min, Max max) {
      current().minimum = static_cast<long double>(min);
      current().maximum = static_cast<long double>(max);
      return *this;
   }

   field_builder& secret() {
      current().secret = true;
      return *this;
   }

   field_builder& deprecated(std::string message) {
      current().deprecated = true;
      current().deprecated_message = std::move(message);
      return *this;
   }

   field_builder& description(std::string text) {
      current().description = std::move(text);
      return *this;
   }

   field_builder& alias(std::string name) {
      current().aliases.push_back(std::move(name));
      return *this;
   }

   template <typename Item> field_builder& items() {
      current().nested_object_list = true;
      current().item_type = std::type_index{typeid(Item)};
      return *this;
   }

   field_builder& non_empty() {
      current().validators.push_back([state = schema_.fields_, index = index_](
                                        const T& object, std::string_view base_path,
                                        std::vector<diagnostic>& diagnostics) {
         const auto& field = (*state)[index];
         if (field.kind != value_kind::string) {
            return;
         }
         const auto any_value = field.read_any(object);
         const auto& value = std::any_cast<const std::string&>(any_value);
         if (value.empty()) {
            diagnostics.push_back(
               make_path_error(append_path(base_path, field.name), "schema.non_empty", "value must not be empty"));
         }
      });
      return *this;
   }

   field_builder& min_items(std::size_t count) {
      current().validators.push_back([state = schema_.fields_, index = index_, count](
                                        const T& object, std::string_view base_path,
                                        std::vector<diagnostic>& diagnostics) {
         const auto& field = (*state)[index];
         if (!field.read_size) {
            return;
         }
         const auto size = field.read_size(object);
         if (size && *size < count) {
            diagnostics.push_back(make_path_error(append_path(base_path, field.name),
                                                 "schema.min_items",
                                                 "list has fewer items than allowed"));
         }
      });
      return *this;
   }

   field_builder& max_items(std::size_t count) {
      current().validators.push_back([state = schema_.fields_, index = index_, count](
                                        const T& object, std::string_view base_path,
                                        std::vector<diagnostic>& diagnostics) {
         const auto& field = (*state)[index];
         if (!field.read_size) {
            return;
         }
         const auto size = field.read_size(object);
         if (size && *size > count) {
            diagnostics.push_back(make_path_error(append_path(base_path, field.name),
                                                 "schema.max_items",
                                                 "list has more items than allowed"));
         }
      });
      return *this;
   }

   field_builder& each_non_empty() {
      current().validators.push_back([state = schema_.fields_, index = index_](
                                        const T& object, std::string_view base_path,
                                        std::vector<diagnostic>& diagnostics) {
         const auto& field = (*state)[index];
         if (field.kind != value_kind::string_list) {
            return;
         }
         const auto any_value = field.read_any(object);
         const auto& values = std::any_cast<const std::vector<std::string>&>(any_value);
         for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i].empty()) {
               diagnostics.push_back(make_path_error(append_index(append_path(base_path, field.name), i),
                                                    "schema.non_empty",
                                                    "list item must not be empty"));
            }
         }
      });
      return *this;
   }

   template <auto Member> field_builder& unique_by() {
      using item_type = typename member_pointer_traits<decltype(Member)>::object_type;
      using member_type = std::remove_cvref_t<typename member_pointer_traits<decltype(Member)>::member_type>;
      current().validators.push_back([state = schema_.fields_, index = index_](
                                        const T& object, std::string_view base_path,
                                        std::vector<diagnostic>& diagnostics) {
         const auto& field = (*state)[index];
         const auto any_value = field.read_any(object);
         const auto& values = std::any_cast<const std::vector<item_type>&>(any_value);
         auto seen = std::set<member_type>{};
         for (const auto& item : values) {
            if (!seen.insert(item.*Member).second) {
               diagnostics.push_back(make_path_error(append_path(base_path, field.name),
                                                    "schema.unique",
                                                    "list items must be unique"));
               return;
            }
         }
      });
      return *this;
   }

   template <auto Member> field_builder field(std::string name) {
      return schema_.template field<Member>(std::move(name));
   }

   [[nodiscard]] operator object_schema<T>() const {
      return schema_;
   }

 private:
   field_rule<T>& current() {
      return schema_.field_at(index_);
   }

   object_schema<T> schema_;
   std::size_t index_ = 0;
};

template <typename T>
[[nodiscard]] T cast_input_to(const input_value& input, std::string_view path, std::vector<diagnostic>& diagnostics) {
   using clean_type = std::remove_cvref_t<T>;
   if constexpr (is_optional<clean_type>::value) {
      using item_type = typename is_optional<clean_type>::value_type;
      return cast_input_to<item_type>(input, path, diagnostics);
   } else if constexpr (std::same_as<clean_type, bool>) {
      if (const auto* value = std::get_if<bool>(&input.storage)) {
         return *value;
      }
      if (const auto* text = std::get_if<std::string>(&input.storage)) {
         auto parsed = false;
         if (parse_bool_text(*text, parsed)) {
            return parsed;
         }
      }
   } else if constexpr (std::signed_integral<clean_type> && !std::same_as<clean_type, bool>) {
      if (const auto* value = std::get_if<std::int64_t>(&input.storage)) {
         return checked_integral_cast<clean_type>(*value);
      }
      if (const auto* value = std::get_if<std::uint64_t>(&input.storage)) {
         return checked_integral_cast<clean_type>(*value);
      }
      if (const auto* text = std::get_if<std::string>(&input.storage)) {
         return parse_scalar_text<clean_type>(*text);
      }
   } else if constexpr (std::unsigned_integral<clean_type> && !std::same_as<clean_type, bool>) {
      if (const auto* value = std::get_if<std::uint64_t>(&input.storage)) {
         return checked_integral_cast<clean_type>(*value);
      }
      if (const auto* value = std::get_if<std::int64_t>(&input.storage); value && *value >= 0) {
         return checked_integral_cast<clean_type>(*value);
      }
      if (const auto* text = std::get_if<std::string>(&input.storage)) {
         return parse_scalar_text<clean_type>(*text);
      }
   } else if constexpr (std::floating_point<clean_type>) {
      if (const auto* value = std::get_if<double>(&input.storage)) {
         return static_cast<T>(*value);
      }
      if (const auto* value = std::get_if<std::int64_t>(&input.storage)) {
         return static_cast<T>(*value);
      }
      if (const auto* value = std::get_if<std::uint64_t>(&input.storage)) {
         return static_cast<T>(*value);
      }
      if (const auto* text = std::get_if<std::string>(&input.storage)) {
         return static_cast<T>(std::stod(*text));
      }
   } else if constexpr (std::is_enum_v<clean_type>) {
      if (const auto* text = std::get_if<std::string>(&input.storage)) {
         auto parsed = clean_type{};
         if (enum_from_config_string(*text, parsed)) {
            return parsed;
         }
      }
      if (const auto* value = std::get_if<std::int64_t>(&input.storage)) {
         auto parsed = clean_type{};
         if (enum_from_int(*value, parsed)) {
            return parsed;
         }
      }
      if (const auto* value = std::get_if<std::uint64_t>(&input.storage);
          value && *value <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
         auto parsed = clean_type{};
         if (enum_from_int(static_cast<std::int64_t>(*value), parsed)) {
            return parsed;
         }
      }
   } else if constexpr (std::same_as<clean_type, std::string>) {
      if (const auto* value = std::get_if<std::string>(&input.storage)) {
         return *value;
      }
   } else if constexpr (std::is_enum_v<clean_type>) {
      if (const auto* text = std::get_if<std::string>(&input.storage)) {
         auto parsed = clean_type{};
         if (enum_from_string(*text, parsed)) {
            return parsed;
         }
         diagnostics.push_back(make_path_error(std::string{path}, "config.enum", "unknown enum value"));
         return {};
      }
      if (const auto* value = std::get_if<std::int64_t>(&input.storage)) {
         auto parsed = clean_type{};
         if (enum_from_int(*value, parsed)) {
            return parsed;
         }
         diagnostics.push_back(make_path_error(std::string{path}, "config.enum", "unknown enum value"));
         return {};
      }
   } else if constexpr (std::same_as<clean_type, std::vector<std::string>>) {
      if (const auto* values = input.as_array()) {
         auto output = std::vector<std::string>{};
         output.reserve(values->size());
         for (std::size_t i = 0; i < values->size(); ++i) {
            if (const auto* text = std::get_if<std::string>(&(*values)[i].storage)) {
               output.push_back(*text);
               continue;
            }
            diagnostics.push_back(make_path_error(append_index(path, i), "config.type", "list entry is not a string"));
         }
         return output;
      }
   } else if constexpr (is_vector_enum<clean_type>::value) {
      using enum_type = typename vector_item<clean_type>::type;
      if (const auto* values = input.as_array()) {
         auto output = clean_type{};
         output.reserve(values->size());
         for (std::size_t i = 0; i < values->size(); ++i) {
            auto parsed = enum_type{};
            if (const auto* text = std::get_if<std::string>(&(*values)[i].storage)) {
               if (enum_from_string(*text, parsed)) {
                  output.push_back(parsed);
                  continue;
               }
               diagnostics.push_back(make_path_error(append_index(path, i), "config.enum", "unknown enum value"));
               continue;
            }
            if (const auto* value = std::get_if<std::int64_t>(&(*values)[i].storage)) {
               if (enum_from_int(*value, parsed)) {
                  output.push_back(parsed);
                  continue;
               }
               diagnostics.push_back(make_path_error(append_index(path, i), "config.enum", "unknown enum value"));
               continue;
            }
            diagnostics.push_back(make_path_error(append_index(path, i), "config.type", "list entry is not an enum"));
         }
         return output;
      }
   } else if constexpr (is_vector<clean_type>::value) {
      using item_type = typename vector_item<clean_type>::type;
      return decode_object_list<item_type>(input, path, diagnostics);
   } else if constexpr (boost::describe::has_describe_members<clean_type>::value) {
      if (const auto* object = input.as_object()) {
         auto output = clean_type{};
         const auto nested_rules = rules<clean_type>::define();
         nested_rules.apply_defaults(output);
         auto nested = nested_rules.decode_object(*object, path, output);
         diagnostics.insert(diagnostics.end(), nested.begin(), nested.end());
         return output;
      }
   }

   throw std::invalid_argument{"config value has incompatible type"};
}

template <typename T>
[[nodiscard]] std::vector<T> decode_object_list(const input_value& input,
                                                std::string_view path,
                                                std::vector<diagnostic>& diagnostics) {
   const auto* values = input.as_array();
   if (!values) {
      throw std::invalid_argument{"config value has incompatible type"};
   }

   auto output = std::vector<T>{};
   output.reserve(values->size());
   const auto nested_rules = rules<T>::define();
   for (std::size_t i = 0; i < values->size(); ++i) {
      const auto item_path = append_index(path, i);
      const auto* object = (*values)[i].as_object();
      if (!object) {
         diagnostics.push_back(make_path_error(item_path, "config.type", "list entry is not an object"));
         continue;
      }
      auto item = T{};
      nested_rules.apply_defaults(item);
      auto nested = nested_rules.decode_object(*object, item_path, item);
      diagnostics.insert(diagnostics.end(), nested.begin(), nested.end());
      output.push_back(std::move(item));
   }
   return output;
}

template <typename T>
[[nodiscard]] input_value to_input_value(const T& input) {
   using clean_type = std::remove_cvref_t<T>;
   if constexpr (is_optional<clean_type>::value) {
      if (!input.has_value()) {
         return input_value{};
      }
      return to_input_value(*input);
   } else if constexpr (std::same_as<clean_type, bool>) {
      return input_value{input};
   } else if constexpr (std::signed_integral<clean_type> && !std::same_as<clean_type, bool>) {
      return input_value{static_cast<std::int64_t>(input)};
   } else if constexpr (std::unsigned_integral<clean_type> && !std::same_as<clean_type, bool>) {
      return input_value{static_cast<std::uint64_t>(input)};
   } else if constexpr (std::floating_point<clean_type>) {
      return input_value{static_cast<double>(input)};
   } else if constexpr (std::same_as<clean_type, std::string>) {
      return input_value{input};
   } else if constexpr (std::is_enum_v<clean_type>) {
      if (auto text = enum_to_config_string(input)) {
         return input_value{std::move(*text)};
      }
      using underlying_type = std::underlying_type_t<clean_type>;
      if constexpr (std::signed_integral<underlying_type>) {
         return input_value{static_cast<std::int64_t>(input)};
      } else {
         return input_value{static_cast<std::uint64_t>(input)};
      }
   } else if constexpr (std::same_as<clean_type, std::vector<std::string>>) {
      auto array = input_value::array_type{};
      array.reserve(input.size());
      for (const auto& item : input) {
         array.emplace_back(item);
      }
      return input_value{std::move(array)};
   } else if constexpr (is_vector_enum<clean_type>::value) {
      auto array = input_value::array_type{};
      array.reserve(input.size());
      for (const auto& item : input) {
         array.push_back(to_input_value(item));
      }
      return input_value{std::move(array)};
   } else if constexpr (is_vector<clean_type>::value) {
      auto array = input_value::array_type{};
      array.reserve(input.size());
      for (const auto& item : input) {
         array.push_back(to_input_value(item));
      }
      return input_value{std::move(array)};
   } else {
      auto object = input_value::object_type{};
      const auto nested_rules = rules<clean_type>::define();
      if (!nested_rules.fields().empty()) {
         for (const auto& field : nested_rules.fields()) {
            auto value = field.read_input(input);
            if (!std::holds_alternative<std::monostate>(value.storage)) {
               object.emplace(field.name, std::move(value));
            }
         }
         return input_value{std::move(object)};
      }
      if constexpr (boost::describe::has_describe_members<clean_type>::value) {
         return input_value{std::move(object)};
      } else {
         return input_value{};
      }
   }
}

template <typename T> [[nodiscard]] object_schema<T> object() {
   return object_schema<T>{};
}

template <typename T> struct rules {
   [[nodiscard]] static object_schema<T> define() {
      return object<T>();
   }
};

} // namespace forge::schema
