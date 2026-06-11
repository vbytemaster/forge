module;

#include <concepts>
#include <string>
#include <type_traits>
#include <vector>

export module fcl.schema.value_kind;

export namespace fcl::schema {

enum class value_kind {
   boolean,
   signed_integer,
   unsigned_integer,
   floating,
   string,
   string_list,
   object_list,
};

template <typename T> struct dependent_false : std::false_type {};

template <typename T> struct member_kind {
   static constexpr value_kind value = [] {
      using clean_type = std::remove_cvref_t<T>;
      if constexpr (std::same_as<clean_type, bool>) {
         return value_kind::boolean;
      } else if constexpr (std::signed_integral<clean_type>) {
         return value_kind::signed_integer;
      } else if constexpr (std::unsigned_integral<clean_type>) {
         return value_kind::unsigned_integer;
      } else if constexpr (std::floating_point<clean_type>) {
         return value_kind::floating;
      } else if constexpr (std::same_as<clean_type, std::string>) {
         return value_kind::string;
      } else if constexpr (std::same_as<clean_type, std::vector<std::string>>) {
         return value_kind::string_list;
      } else {
         static_assert(dependent_false<clean_type>::value, "unsupported FCL schema field type");
      }
   }();
};

} // namespace fcl::schema
