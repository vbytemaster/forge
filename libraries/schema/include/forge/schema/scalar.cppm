module;

#include <boost/describe.hpp>
#include <boost/mp11/algorithm.hpp>

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

import forge.schema.enums;

export namespace forge::schema {

template <typename T> struct scalar_optional : std::false_type {};
template <typename T> struct scalar_optional<std::optional<T>> : std::true_type {
   using value_type = T;
};

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

template <typename Target, typename Source>
[[nodiscard]] Target checked_integral_cast(Source value) {
   using limits = std::numeric_limits<Target>;
   if constexpr (std::signed_integral<Source> && std::signed_integral<Target>) {
      if constexpr (std::numeric_limits<Target>::digits < std::numeric_limits<Source>::digits) {
         const auto input = static_cast<std::intmax_t>(value);
         if (input < static_cast<std::intmax_t>((limits::min)()) ||
             input > static_cast<std::intmax_t>((limits::max)())) {
            throw std::invalid_argument{"integer is outside target type range"};
         }
      }
   } else if constexpr (std::signed_integral<Source> && std::unsigned_integral<Target>) {
      if (value < 0) {
         throw std::invalid_argument{"integer is outside target type range"};
      }
      if constexpr (std::numeric_limits<Target>::digits < std::numeric_limits<Source>::digits) {
         if (static_cast<std::uintmax_t>(value) > static_cast<std::uintmax_t>((limits::max)())) {
            throw std::invalid_argument{"integer is outside target type range"};
         }
      }
   } else if constexpr (std::unsigned_integral<Source> && std::signed_integral<Target>) {
      if constexpr (std::numeric_limits<Target>::digits < std::numeric_limits<Source>::digits) {
         if (static_cast<std::uintmax_t>(value) > static_cast<std::uintmax_t>((limits::max)())) {
            throw std::invalid_argument{"integer is outside target type range"};
         }
      }
   } else {
      if constexpr (std::numeric_limits<Target>::digits < std::numeric_limits<Source>::digits) {
         if (static_cast<std::uintmax_t>(value) > static_cast<std::uintmax_t>((limits::max)())) {
            throw std::invalid_argument{"integer is outside target type range"};
         }
      }
   }
   return static_cast<Target>(value);
}

template <typename Value>
[[nodiscard]] Value parse_integral_text(std::string_view text) {
   if (text.empty()) {
      throw std::invalid_argument{"integer has invalid syntax"};
   }
   auto value = Value{};
   const auto* first = text.data();
   const auto* last = text.data() + text.size();
   const auto [next, error] = std::from_chars(first, last, value);
   if (error == std::errc::result_out_of_range) {
      throw std::invalid_argument{"integer is outside target type range"};
   }
   if (error != std::errc{} || next != last) {
      throw std::invalid_argument{"integer has invalid syntax"};
   }
   return value;
}

template <typename T>
[[nodiscard]] T parse_scalar_text(std::string_view text) {
   using clean = std::remove_cvref_t<T>;
   if constexpr (std::same_as<clean, std::string>) {
      return std::string{text};
   } else if constexpr (std::same_as<clean, bool>) {
      auto parsed = false;
      if (!parse_bool_text(std::string{text}, parsed)) {
         throw std::invalid_argument{"boolean has invalid syntax"};
      }
      return parsed;
   } else if constexpr (std::signed_integral<clean> && !std::same_as<clean, bool>) {
      return checked_integral_cast<clean>(parse_integral_text<std::int64_t>(text));
   } else if constexpr (std::unsigned_integral<clean> && !std::same_as<clean, bool>) {
      return checked_integral_cast<clean>(parse_integral_text<std::uint64_t>(text));
   } else if constexpr (std::floating_point<clean>) {
      auto copy = std::string{text};
      char* end = nullptr;
      errno = 0;
      const auto parsed = std::strtold(copy.c_str(), &end);
      if (errno != 0 || end != copy.c_str() + copy.size()) {
         throw std::invalid_argument{"floating-point value has invalid syntax"};
      }
      return static_cast<clean>(parsed);
   } else if constexpr (std::is_enum_v<clean>) {
      auto parsed = clean{};
      if (enum_from_config_string(text, parsed)) {
         return parsed;
      }
      throw std::invalid_argument{"enum value is invalid"};
   } else {
      static_assert(sizeof(clean) == 0, "parse_scalar_text requires a scalar text type");
   }
}

template <typename T>
[[nodiscard]] std::optional<std::string> format_scalar_text(const T& value) {
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
   } else if constexpr (std::integral<clean> && !std::same_as<clean, bool>) {
      return std::to_string(value);
   } else if constexpr (std::floating_point<clean>) {
      auto stream = std::ostringstream{};
      stream << value;
      return stream.str();
   } else if constexpr (std::is_enum_v<clean>) {
      return enum_to_config_string(value);
   } else {
      return std::nullopt;
   }
}

} // namespace forge::schema
