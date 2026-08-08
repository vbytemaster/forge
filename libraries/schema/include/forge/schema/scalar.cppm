module;

#include <boost/describe.hpp>
#include <boost/mp11/algorithm.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <concepts>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

export module forge.schema.scalar;

import forge.schema.exceptions;
import forge.schema.enums;
import forge.schema.value_kind;

export namespace forge::schema {

template <typename T> struct scalar_optional : std::false_type {};
template <typename T> struct scalar_optional<std::optional<T>> : std::true_type {
   using value_type = T;
};

template <typename T>
concept canonical_string_scalar =
    (std::constructible_from<std::remove_cvref_t<T>, std::string> ||
     requires(std::string_view text) {
        { std::remove_cvref_t<T>::from_string(text) } -> std::same_as<std::remove_cvref_t<T>>;
     }) &&
    (std::convertible_to<const std::remove_cvref_t<T>&, std::string> || requires(const std::remove_cvref_t<T>& value) {
       { value.str() } -> std::convertible_to<std::string>;
    } || requires(const std::remove_cvref_t<T>& value) {
       { value.to_string() } -> std::convertible_to<std::string>;
    });

[[nodiscard]] inline bool parse_bool_text(std::string text, bool& output) {
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

template <typename Target, typename Source> [[nodiscard]] Target checked_integral_cast(Source value) {
   static_assert(integral_value<Target> && integral_value<Source>);
   using limits = std::numeric_limits<Target>;
   if constexpr (signed_integral_value<Source> && signed_integral_value<Target>) {
      if constexpr (std::numeric_limits<Target>::digits < std::numeric_limits<Source>::digits) {
         if (value < static_cast<Source>((limits::min)()) || value > static_cast<Source>((limits::max)())) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_value, "integer is outside target type range");
         }
      }
   } else if constexpr (signed_integral_value<Source> && unsigned_integral_value<Target>) {
      if (value < 0) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_value, "integer is outside target type range");
      }
      if constexpr (std::numeric_limits<Target>::digits < std::numeric_limits<Source>::digits) {
         using unsigned_source = unsigned_integral_t<Source>;
         if (static_cast<unsigned_source>(value) > static_cast<unsigned_source>((limits::max)())) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_value, "integer is outside target type range");
         }
      }
   } else if constexpr (unsigned_integral_value<Source> && signed_integral_value<Target>) {
      if constexpr (std::numeric_limits<Target>::digits < std::numeric_limits<Source>::digits) {
         if (value > static_cast<Source>((limits::max)())) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_value, "integer is outside target type range");
         }
      }
   } else {
      if constexpr (std::numeric_limits<Target>::digits < std::numeric_limits<Source>::digits) {
         if (value > static_cast<Source>((limits::max)())) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_value, "integer is outside target type range");
         }
      }
   }
   return static_cast<Target>(value);
}

template <typename Value> [[nodiscard]] Value parse_integral_text(std::string_view text) {
   static_assert(integral_value<Value> && !std::same_as<Value, bool>);
   if (text.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_value, "integer has invalid syntax");
   }

   using unsigned_value = unsigned_integral_t<Value>;
   auto position = std::size_t{0};
   auto negative = false;
   if constexpr (signed_integral_value<Value>) {
      negative = text.front() == '-';
      position = negative ? 1 : 0;
   }
   if (position == text.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_value, "integer has invalid syntax");
   }

   const auto positive_limit = static_cast<unsigned_value>((std::numeric_limits<Value>::max)());
   const auto limit = negative ? static_cast<unsigned_value>(positive_limit + unsigned_value{1}) : positive_limit;
   auto magnitude = unsigned_value{};
   for (; position < text.size(); ++position) {
      const auto character = text[position];
      if (character < '0' || character > '9') {
         FORGE_THROW_EXCEPTION(exceptions::invalid_value, "integer has invalid syntax");
      }
      const auto digit = static_cast<unsigned_value>(character - '0');
      if (magnitude > static_cast<unsigned_value>((limit - digit) / unsigned_value{10})) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_value, "integer is outside target type range");
      }
      magnitude = static_cast<unsigned_value>(magnitude * unsigned_value{10} + digit);
   }

   if constexpr (signed_integral_value<Value>) {
      if (negative) {
         if (magnitude == static_cast<unsigned_value>(positive_limit + unsigned_value{1})) {
            return (std::numeric_limits<Value>::min)();
         }
         return static_cast<Value>(-static_cast<Value>(magnitude));
      }
   }
   return static_cast<Value>(magnitude);
}

template <typename T> [[nodiscard]] T parse_scalar_text(std::string_view text) {
   using clean = std::remove_cvref_t<T>;
   if constexpr (std::same_as<clean, std::string>) {
      return std::string{text};
   } else if constexpr (std::same_as<clean, bool>) {
      auto parsed = false;
      if (!parse_bool_text(std::string{text}, parsed)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_value, "boolean has invalid syntax");
      }
      return parsed;
   } else if constexpr (signed_integral_value<clean> && !std::same_as<clean, bool>) {
      return parse_integral_text<clean>(text);
   } else if constexpr (unsigned_integral_value<clean> && !std::same_as<clean, bool>) {
      return parse_integral_text<clean>(text);
   } else if constexpr (std::floating_point<clean>) {
      auto copy = std::string{text};
      char* end = nullptr;
      errno = 0;
      const auto parsed = std::strtold(copy.c_str(), &end);
      if (errno != 0 || end != copy.c_str() + copy.size()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_value, "floating-point value has invalid syntax");
      }
      return static_cast<clean>(parsed);
   } else if constexpr (std::is_enum_v<clean>) {
      auto parsed = clean{};
      if (enum_from_config_string(text, parsed)) {
         return parsed;
      }
      FORGE_THROW_EXCEPTION(exceptions::invalid_value, "enum value is invalid");
   } else if constexpr (canonical_string_scalar<clean>) {
      try {
         if constexpr (requires { clean::from_string(text); }) {
            return clean::from_string(text);
         } else {
            return clean{std::string{text}};
         }
      } catch (const forge::exceptions::base& error) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_value, "canonical scalar value is invalid",
                               forge::exceptions::ctx("reason", error.message()));
      } catch (const std::exception& error) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_value, "canonical scalar value is invalid",
                               forge::exceptions::ctx("reason", error.what()));
      }
   } else {
      static_assert(sizeof(clean) == 0, "parse_scalar_text requires a scalar text type");
   }
}

template <integral_value Value> [[nodiscard]] std::string format_integral_text(Value value) {
   using unsigned_value = unsigned_integral_t<Value>;
   const auto negative = signed_integral_value<Value> && value < 0;
   auto magnitude = static_cast<unsigned_value>(value);
   if (negative) {
      magnitude = static_cast<unsigned_value>(unsigned_value{0} - magnitude);
   }

   auto output = std::string{};
   do {
      output.push_back(static_cast<char>('0' + magnitude % unsigned_value{10}));
      magnitude /= unsigned_value{10};
   } while (magnitude != 0);
   if (negative) {
      output.push_back('-');
   }
   std::ranges::reverse(output);
   return output;
}

template <typename T> [[nodiscard]] std::optional<std::string> format_scalar_text(const T& value) {
   using clean = std::remove_cvref_t<T>;
   if constexpr (scalar_optional<clean>::value) {
      if (!value.has_value()) {
         return std::nullopt;
      }
      return format_scalar_text(*value);
   } else if constexpr (std::same_as<clean, std::string>) {
      return value;
   } else if constexpr (std::same_as<clean, bool>) {
      return value ? std::string{"true"} : std::string{"false"};
   } else if constexpr (integral_value<clean> && !std::same_as<clean, bool>) {
      return format_integral_text(value);
   } else if constexpr (std::floating_point<clean>) {
      auto stream = std::ostringstream{};
      stream << value;
      return stream.str();
   } else if constexpr (std::is_enum_v<clean>) {
      return enum_to_config_string(value);
   } else if constexpr (canonical_string_scalar<clean>) {
      try {
         if constexpr (std::convertible_to<const clean&, std::string>) {
            return static_cast<std::string>(value);
         } else if constexpr (requires { value.str(); }) {
            return value.str();
         } else {
            return value.to_string();
         }
      } catch (const forge::exceptions::base&) {
         throw;
      } catch (const std::exception& error) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_value, "canonical scalar value cannot be formatted",
                               forge::exceptions::ctx("reason", error.what()));
      }
   } else {
      return std::nullopt;
   }
}

} // namespace forge::schema
