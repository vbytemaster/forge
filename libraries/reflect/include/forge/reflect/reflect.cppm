module;
#include <boost/describe.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/mp11.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

export module forge.reflect.reflect;

import forge.core.type_name;

export namespace forge::reflect {

template <typename T> using clean_type = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
inline constexpr bool is_described_object_v = boost::describe::has_describe_members<clean_type<T>>::value;

template <typename T>
inline constexpr bool is_described_enum_v =
    std::is_enum_v<clean_type<T>> && boost::describe::has_describe_enumerators<clean_type<T>>::value;

template <typename T, typename Visitor> void for_each_member(Visitor&& visitor) {
   using members = boost::describe::describe_members<clean_type<T>,
                                                     boost::describe::mod_any_access | boost::describe::mod_inherited>;

   boost::mp11::mp_for_each<members>(
       [&](auto descriptor) { std::forward<Visitor>(visitor)(descriptor.name, descriptor.pointer); });
}

template <typename Enum> const char* enum_to_string(Enum value) {
   static_assert(is_described_enum_v<Enum>, "Enum must be described with BOOST_DESCRIBE_ENUM");

   const char* result = nullptr;
   using enumerators = boost::describe::describe_enumerators<clean_type<Enum>>;
   boost::mp11::mp_for_each<enumerators>([&](auto descriptor) {
      if (descriptor.value == value)
         result = descriptor.name;
   });

   if (!result)
      throw std::invalid_argument("bad enum cast for " + std::string(forge::type_name<clean_type<Enum>>()));

   return result;
}

template <typename Enum> std::string enum_to_fc_string(Enum value) {
   static_assert(is_described_enum_v<Enum>, "Enum must be described with BOOST_DESCRIBE_ENUM");

   const char* result = nullptr;
   using enumerators = boost::describe::describe_enumerators<clean_type<Enum>>;
   boost::mp11::mp_for_each<enumerators>([&](auto descriptor) {
      if (descriptor.value == value)
         result = descriptor.name;
   });

   if (result)
      return result;

   return std::to_string(static_cast<int64_t>(value));
}

template <typename Enum> Enum enum_from_int(int64_t value) {
   static_assert(is_described_enum_v<Enum>, "Enum must be described with BOOST_DESCRIBE_ENUM");

   bool found = false;
   Enum result{};
   using enumerators = boost::describe::describe_enumerators<clean_type<Enum>>;
   boost::mp11::mp_for_each<enumerators>([&](auto descriptor) {
      if (static_cast<int64_t>(descriptor.value) == value) {
         found = true;
         result = descriptor.value;
      }
   });

   if (!found)
      throw std::invalid_argument("bad enum integer cast for " + std::string(forge::type_name<clean_type<Enum>>()));

   return result;
}

template <typename Enum> Enum enum_from_string(const char* value) {
   static_assert(is_described_enum_v<Enum>, "Enum must be described with BOOST_DESCRIBE_ENUM");

   bool found = false;
   Enum result{};
   using enumerators = boost::describe::describe_enumerators<clean_type<Enum>>;
   boost::mp11::mp_for_each<enumerators>([&](auto descriptor) {
      if (std::string(descriptor.name) == value) {
         found = true;
         result = descriptor.value;
      }
   });

   if (found)
      return result;

   try {
      return enum_from_int<Enum>(boost::lexical_cast<int64_t>(value));
   } catch (const boost::bad_lexical_cast&) {
      throw std::invalid_argument("bad enum string cast for " + std::string(forge::type_name<clean_type<Enum>>()));
   }

   return Enum{};
}

} // namespace forge::reflect
