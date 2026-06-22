module;

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module fcl.config.component;

import fcl.config.document;
import fcl.config.value;
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;
import fcl.schema.scalar;

export namespace fcl::config {

struct field_descriptor {
   std::string name;
   std::vector<std::string> aliases;
   schema::value_kind kind = schema::value_kind::string;
   bool required = false;
   bool has_default = false;
   value default_value;
   bool secret = false;
   bool deprecated = false;
   std::string deprecated_message;
   std::string description;
};

struct component_descriptor {
   std::string section;
   std::vector<field_descriptor> fields;
};

class component_registry {
 public:
   void add(component_descriptor descriptor);

   [[nodiscard]] const std::vector<component_descriptor>& components() const noexcept {
      return components_;
   }

 private:
   std::vector<component_descriptor> components_;
};

class component_view {
 public:
   component_view(const document& source, std::string section) : source_{&source}, section_{std::move(section)} {}

   [[nodiscard]] const document& source() const noexcept {
      return *source_;
   }

   [[nodiscard]] std::string_view section() const noexcept {
      return section_;
   }

   [[nodiscard]] const value* try_get(std::string_view field) const;

   template <typename T> [[nodiscard]] T get_or(std::string_view field, T fallback) const {
      const auto* found = try_get(field);
      if (!found) {
         return fallback;
      }
      return convert_value<T>(*found);
   }

 private:
   template <typename T> [[nodiscard]] static T convert_value(const value& input) {
      using clean_type = std::remove_cvref_t<T>;
      if constexpr (std::same_as<clean_type, bool>) {
         if (const auto* bool_value = std::get_if<bool>(&input.storage)) {
            return *bool_value;
         }
      } else if constexpr (std::signed_integral<clean_type>) {
         if (const auto* signed_value = std::get_if<std::int64_t>(&input.storage)) {
            return schema::checked_integral_cast<clean_type>(*signed_value);
         }
         if (const auto* unsigned_value = std::get_if<std::uint64_t>(&input.storage)) {
            return schema::checked_integral_cast<clean_type>(*unsigned_value);
         }
      } else if constexpr (std::unsigned_integral<clean_type>) {
         if (const auto* unsigned_value = std::get_if<std::uint64_t>(&input.storage)) {
            return schema::checked_integral_cast<clean_type>(*unsigned_value);
         }
         if (const auto* signed_value = std::get_if<std::int64_t>(&input.storage)) {
            return schema::checked_integral_cast<clean_type>(*signed_value);
         }
      } else if constexpr (std::floating_point<clean_type>) {
         if (const auto* double_value = std::get_if<double>(&input.storage)) {
            return static_cast<clean_type>(*double_value);
         }
         if (const auto* signed_value = std::get_if<std::int64_t>(&input.storage)) {
            return static_cast<clean_type>(*signed_value);
         }
         if (const auto* unsigned_value = std::get_if<std::uint64_t>(&input.storage)) {
            return static_cast<clean_type>(*unsigned_value);
         }
      } else if constexpr (std::same_as<clean_type, std::string>) {
         if (const auto* string_value = std::get_if<std::string>(&input.storage)) {
            return *string_value;
         }
      }
      throw std::invalid_argument{"config value has incompatible type"};
   }

   const document* source_ = nullptr;
   std::string section_;
};

[[nodiscard]] document redact(document input, const component_registry& registry);
[[nodiscard]] document defaults_for(const component_registry& registry);

} // namespace fcl::config
