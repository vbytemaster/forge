module;

#include <boost/describe.hpp>
#include <boost/mp11/algorithm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

export module forge.schema.enums;

import forge.schema.diagnostic;
import forge.schema.value_kind;

export namespace forge::schema {

[[nodiscard]] inline bool enum_from_string(std::string_view name, severity& out) {
   if (name == "info") {
      out = severity::info;
      return true;
   }
   if (name == "warning") {
      out = severity::warning;
      return true;
   }
   if (name == "error") {
      out = severity::error;
      return true;
   }
   return false;
}

[[nodiscard]] inline std::optional<std::string> enum_to_string(severity value) {
   switch (value) {
   case severity::info:
      return "info";
   case severity::warning:
      return "warning";
   case severity::error:
      return "error";
   }
   return std::nullopt;
}

[[nodiscard]] inline bool enum_from_int(std::int64_t value, severity& out) {
   if (value == 0) {
      out = severity::info;
      return true;
   }
   if (value == 1) {
      out = severity::warning;
      return true;
   }
   if (value == 2) {
      out = severity::error;
      return true;
   }
   return false;
}

template <typename E> [[nodiscard]] bool enum_from_string(std::string_view name, E& out) {
   static_assert(std::is_enum_v<E>, "enum_from_string requires an enum type");
   auto matched = false;
   boost::mp11::mp_for_each<boost::describe::describe_enumerators<E>>([&](auto descriptor) {
      if (!matched && name == descriptor.name) {
         out = descriptor.value;
         matched = true;
      }
   });
   return matched;
}

[[nodiscard]] inline std::string config_enum_name(std::string_view value) {
   auto output = std::string{value};
   for (auto& ch : output) {
      if (ch == '_') {
         ch = '-';
      }
   }
   return output;
}

[[nodiscard]] inline std::string enum_identifier(std::string_view value) {
   auto output = std::string{value};
   for (auto& ch : output) {
      if (ch == '-') {
         ch = '_';
      }
   }
   return output;
}

template <typename E> [[nodiscard]] bool enum_from_config_string(std::string_view name, E& out) {
   static_assert(std::is_enum_v<E>, "enum_from_config_string requires an enum type");
   if (enum_from_string(name, out)) {
      return true;
   }
   const auto identifier = enum_identifier(name);
   return enum_from_string(identifier, out);
}

template <typename E> [[nodiscard]] std::optional<std::string> enum_to_string(E value) {
   static_assert(std::is_enum_v<E>, "enum_to_string requires an enum type");
   auto result = std::optional<std::string>{};
   boost::mp11::mp_for_each<boost::describe::describe_enumerators<E>>([&](auto descriptor) {
      if (!result && value == descriptor.value) {
         result = descriptor.name;
      }
   });
   return result;
}

template <typename E> [[nodiscard]] std::optional<std::string> enum_to_config_string(E value) {
   static_assert(std::is_enum_v<E>, "enum_to_config_string requires an enum type");
   auto name = enum_to_string(value);
   if (!name) {
      return std::nullopt;
   }
   return config_enum_name(*name);
}

template <typename E> [[nodiscard]] bool enum_from_int(std::int64_t value, E& out) {
   static_assert(std::is_enum_v<E>, "enum_from_int requires an enum type");
   auto matched = false;
   boost::mp11::mp_for_each<boost::describe::describe_enumerators<E>>([&](auto descriptor) {
      if (!matched && static_cast<std::int64_t>(descriptor.value) == value) {
         out = descriptor.value;
         matched = true;
      }
   });
   return matched;
}

} // namespace forge::schema
