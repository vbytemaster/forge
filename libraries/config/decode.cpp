module;

#include <algorithm>
#include <any>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

module fcl.config.decode;

import fcl.config.value;
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

namespace fcl::config {

bool parse_bool_text(std::string text, bool& output) {
   std::ranges::transform(text, text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
   if (text == "true" || text == "1" || text == "yes" || text == "on") {
      output = true;
      return true;
   }
   if (text == "false" || text == "0" || text == "no" || text == "off") {
      output = false;
      return true;
   }
   return false;
}

schema::input_value to_schema_value(const value& input) {
   if (const auto* bool_value = std::get_if<bool>(&input.storage)) {
      return schema::input_value{*bool_value};
   }
   if (const auto* signed_value = std::get_if<std::int64_t>(&input.storage)) {
      return schema::input_value{*signed_value};
   }
   if (const auto* unsigned_value = std::get_if<std::uint64_t>(&input.storage)) {
      return schema::input_value{*unsigned_value};
   }
   if (const auto* floating_value = std::get_if<double>(&input.storage)) {
      return schema::input_value{*floating_value};
   }
   if (const auto* string_value = std::get_if<std::string>(&input.storage)) {
      return schema::input_value{*string_value};
   }
   if (const auto* array_value = input.as_array()) {
      auto out = schema::input_value::array_type{};
      out.reserve(array_value->size());
      for (const auto& item : *array_value) {
         out.push_back(to_schema_value(item));
      }
      return schema::input_value{std::move(out)};
   }
   if (const auto* object_value = input.as_object()) {
      auto out = schema::input_value::object_type{};
      for (const auto& [name, item] : *object_value) {
         out.emplace(name, to_schema_value(item));
      }
      return schema::input_value{std::move(out)};
   }
   return schema::input_value{};
}

std::optional<std::int64_t> parse_signed_integer(std::string_view input) {
   auto parsed = std::int64_t{};
   const auto* first = input.data();
   const auto* last = input.data() + input.size();
   const auto [ptr, ec] = std::from_chars(first, last, parsed);
   if (ec != std::errc{} || ptr != last) {
      return std::nullopt;
   }
   return parsed;
}

std::optional<std::uint64_t> parse_unsigned_integer(std::string_view input) {
   auto parsed = std::uint64_t{};
   const auto* first = input.data();
   const auto* last = input.data() + input.size();
   const auto [ptr, ec] = std::from_chars(first, last, parsed);
   if (ec != std::errc{} || ptr != last) {
      return std::nullopt;
   }
   return parsed;
}

std::any value_to_any(const value& input, schema::value_kind kind) {
   switch (kind) {
   case schema::value_kind::boolean:
      if (const auto* bool_value = std::get_if<bool>(&input.storage)) {
         return *bool_value;
      }
      if (const auto* string_value = std::get_if<std::string>(&input.storage)) {
         auto parsed = false;
         if (parse_bool_text(*string_value, parsed)) {
            return parsed;
         }
      }
      break;
   case schema::value_kind::signed_integer:
      if (const auto* signed_value = std::get_if<std::int64_t>(&input.storage)) {
         return *signed_value;
      }
      if (const auto* unsigned_value = std::get_if<std::uint64_t>(&input.storage)) {
         if (*unsigned_value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            break;
         }
         return static_cast<std::int64_t>(*unsigned_value);
      }
      if (const auto* string_value = std::get_if<std::string>(&input.storage)) {
         if (const auto parsed = parse_signed_integer(*string_value)) {
            return *parsed;
         }
      }
      break;
   case schema::value_kind::unsigned_integer:
      if (const auto* unsigned_value = std::get_if<std::uint64_t>(&input.storage)) {
         return *unsigned_value;
      }
      if (const auto* signed_value = std::get_if<std::int64_t>(&input.storage)) {
         if (*signed_value < 0) {
            break;
         }
         return static_cast<std::uint64_t>(*signed_value);
      }
      if (const auto* string_value = std::get_if<std::string>(&input.storage)) {
         if (const auto parsed = parse_unsigned_integer(*string_value)) {
            return *parsed;
         }
      }
      break;
   case schema::value_kind::floating:
      if (const auto* double_value = std::get_if<double>(&input.storage)) {
         return *double_value;
      }
      if (const auto* signed_value = std::get_if<std::int64_t>(&input.storage)) {
         return static_cast<double>(*signed_value);
      }
      if (const auto* unsigned_value = std::get_if<std::uint64_t>(&input.storage)) {
         return static_cast<double>(*unsigned_value);
      }
      if (const auto* string_value = std::get_if<std::string>(&input.storage)) {
         return std::stod(*string_value);
      }
      break;
   case schema::value_kind::string:
      if (const auto* string_value = std::get_if<std::string>(&input.storage)) {
         return *string_value;
      }
      break;
   case schema::value_kind::string_list:
      if (const auto* array = input.as_array()) {
         auto strings = std::vector<std::string>{};
         strings.reserve(array->size());
         for (const auto& entry : *array) {
            const auto* string_value = std::get_if<std::string>(&entry.storage);
            if (!string_value) {
               throw std::invalid_argument{"list entry is not a string"};
            }
            strings.push_back(*string_value);
         }
         return strings;
      }
      break;
   case schema::value_kind::object_list:
      if (const auto* array = input.as_array()) {
         for (const auto& entry : *array) {
            if (!entry.as_object()) {
               throw std::invalid_argument{"list entry is not an object"};
            }
         }
         return *array;
      }
      break;
   }
   throw std::invalid_argument{"config value has incompatible type"};
}

std::string format_decode_diagnostics(std::string_view prefix, const decode_diagnostics& diagnostics) {
   auto output = std::string{prefix};
   if (diagnostics.entries.empty()) {
      return output;
   }

   output += ": ";
   auto first = true;
   for (const auto& entry : diagnostics.entries) {
      if (!first) {
         output += "; ";
      }
      first = false;
      output += entry.path;
      output += " ";
      output += entry.code;
      output += " ";
      output += entry.message;
   }
   return output;
}

value any_to_value(schema::value_kind kind, const std::any& input) {
   switch (kind) {
   case schema::value_kind::boolean:
      return schema::cast_any_to<bool>(input);
   case schema::value_kind::signed_integer:
      return schema::cast_any_to<std::int64_t>(input);
   case schema::value_kind::unsigned_integer:
      return schema::cast_any_to<std::uint64_t>(input);
   case schema::value_kind::floating:
      return schema::cast_any_to<double>(input);
   case schema::value_kind::string:
      return schema::cast_any_to<std::string>(input);
   case schema::value_kind::string_list: {
      auto array = value::array_type{};
      for (const auto& entry : schema::cast_any_to<std::vector<std::string>>(input)) {
         array.emplace_back(entry);
      }
      return array;
   }
   case schema::value_kind::object_list:
      return schema::cast_any_to<value::array_type>(input);
   }
   return {};
}

} // namespace fcl::config
