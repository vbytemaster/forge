module;

#include <boost/describe.hpp>

#include <concepts>
#include <cstdint>
#include <type_traits>

export module forge.objectdb.object;

import forge.ids.object_id;

export namespace forge::objectdb {

template <typename Derived, std::uint8_t Space, std::uint16_t Type>
struct object {
   using derived_type = Derived;
   using id_type = forge::ids::typed_id<Space, Type>;

   static constexpr std::uint8_t space = Space;
   static constexpr std::uint16_t type = Type;

   id_type id;

   bool operator==(const object&) const = default;
   auto operator<=>(const object&) const = default;

   BOOST_DESCRIBE_CLASS(object, (), (id), (), ())
};

template <typename T>
struct object_base_traits {
   static constexpr bool is_object_base = false;
};

template <typename Derived, std::uint8_t Space, std::uint16_t Type>
struct object_base_traits<object<Derived, Space, Type>> {
   static constexpr bool is_object_base = true;
   using derived_type = Derived;
   using id_type = forge::ids::typed_id<Space, Type>;
   static constexpr std::uint8_t space = Space;
   static constexpr std::uint16_t type = Type;
};

template <typename T>
concept object_value = requires {
   typename std::remove_cvref_t<T>::id_type;
   { std::remove_cvref_t<T>::space } -> std::convertible_to<std::uint8_t>;
   { std::remove_cvref_t<T>::type } -> std::convertible_to<std::uint16_t>;
} && forge::ids::typed_id_traits<typename std::remove_cvref_t<T>::id_type>::is_typed_id &&
   std::derived_from<std::remove_cvref_t<T>,
                     object<std::remove_cvref_t<T>, std::remove_cvref_t<T>::space, std::remove_cvref_t<T>::type>>;

template <typename Id>
using object_index_for_id_t = forge::ids::type_for_id_t<Id>;

} // namespace forge::objectdb
