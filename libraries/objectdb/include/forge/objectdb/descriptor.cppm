module;

#include <boost/describe.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

export module forge.objectdb.descriptor;

import forge.ids.types;
import forge.objectdb.types;

export namespace forge::objectdb {

template <typename T>
struct typed_id_traits {
   static constexpr bool is_typed_id = false;
};

template <std::uint8_t Space, std::uint16_t Type>
struct typed_id_traits<forge::ids::typed_id<Space, Type>> {
   static constexpr bool is_typed_id = true;
   static constexpr std::uint8_t space = Space;
   static constexpr std::uint16_t type = Type;
};

template <typename T>
concept typed_object_id = typed_id_traits<std::remove_cvref_t<T>>::is_typed_id;

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
} && typed_id_traits<typename std::remove_cvref_t<T>::id_type>::is_typed_id &&
   std::derived_from<std::remove_cvref_t<T>,
                     object<std::remove_cvref_t<T>, std::remove_cvref_t<T>::space, std::remove_cvref_t<T>::type>>;

template <auto Extractor>
struct extractor_traits;

template <typename Owner, typename Value, Value Owner::* Member>
struct extractor_traits<Member> {
   using owner_type = Owner;
   using value_type = Value;

   static constexpr auto pointer = Member;

   [[nodiscard]] static constexpr decltype(auto) get(const Owner& value) noexcept {
      return value.*Member;
   }
};

template <typename Owner, typename Value, Value (*Function)(const Owner&)>
struct extractor_traits<Function> {
   using owner_type = Owner;
   using value_type = Value;

   static constexpr auto pointer = Function;

   [[nodiscard]] static constexpr decltype(auto) get(const Owner& value) noexcept(noexcept(Function(value))) {
      return Function(value);
   }
};

template <auto Extractor>
using member_pointer_traits = extractor_traits<Extractor>;

template <auto Extractor>
struct member_key {
   using owner_type = typename extractor_traits<Extractor>::owner_type;
   using value_type = typename extractor_traits<Extractor>::value_type;

   static constexpr auto extractor = Extractor;
   static constexpr std::size_t size = 1;
};

template <auto... Extractors>
struct composite_key {
   static_assert(sizeof...(Extractors) > 0, "forge::objectdb::composite_key requires at least one member");

 private:
   template <auto First, auto... Rest>
   struct first_extractor {
      using owner_type = typename extractor_traits<First>::owner_type;
   };

 public:
   using owner_type = typename first_extractor<Extractors...>::owner_type;

   static constexpr std::size_t size = sizeof...(Extractors);
};

struct primary_key {
   static constexpr std::size_t size = 1;
};

template <typename Tag>
struct primary_unique {
   using tag_type = Tag;
   using key_spec = primary_key;

   static constexpr index_kind kind = index_kind::primary_unique;
};

template <typename Tag, auto Extractor>
struct secondary_unique {
   using tag_type = Tag;
   using owner_type = typename extractor_traits<Extractor>::owner_type;
   using member_type = typename extractor_traits<Extractor>::value_type;
   using key_spec = member_key<Extractor>;

   static constexpr auto extractor = Extractor;
   static constexpr index_kind kind = index_kind::secondary_unique;
};

template <typename Tag, typename KeySpec>
struct secondary_non_unique {
   using tag_type = Tag;
   using owner_type = typename KeySpec::owner_type;
   using key_spec = KeySpec;

   static constexpr index_kind kind = index_kind::secondary_non_unique;
};

template <typename... Indexes>
struct indexed_by {
   using tuple_type = std::tuple<Indexes...>;
   static constexpr std::size_t size = sizeof...(Indexes);
};

template <typename Value, bool Valid>
struct object_index_value_traits {};

template <typename Value>
struct object_index_value_traits<Value, true> {
   using base_type = object<Value, Value::space, Value::type>;
   using id_type = typename Value::id_type;
};

template <typename Value, typename Indexes>
struct object_index : object_index_value_traits<Value, object_value<Value>> {
   using value_type = Value;
   using indexes_type = Indexes;
};

template <typename T>
struct is_primary_index : std::false_type {};

template <typename Tag>
struct is_primary_index<primary_unique<Tag>> : std::true_type {};

template <typename T>
inline constexpr bool is_primary_index_v = is_primary_index<T>::value;

template <typename T>
struct is_secondary_index : std::false_type {};

template <typename Tag, auto Extractor>
struct is_secondary_index<secondary_unique<Tag, Extractor>> : std::true_type {};

template <typename Tag, typename KeySpec>
struct is_secondary_index<secondary_non_unique<Tag, KeySpec>> : std::true_type {};

template <typename T>
inline constexpr bool is_secondary_index_v = is_secondary_index<T>::value;

template <typename T>
concept index_model = requires {
   typename T::tag_type;
   typename T::key_spec;
   { T::kind } -> std::convertible_to<index_kind>;
};

template <typename T>
concept primary_index = index_model<T> && is_primary_index_v<T>;

template <typename T>
concept secondary_index = index_model<T> && is_secondary_index_v<T>;

namespace detail {

template <typename T>
struct is_indexed_by : std::false_type {};

template <typename... Indexes>
struct is_indexed_by<indexed_by<Indexes...>> : std::true_type {};

template <typename T>
inline constexpr bool is_indexed_by_v = is_indexed_by<T>::value;

template <typename Indexes>
struct primary_count;

template <typename... Indexes>
struct primary_count<indexed_by<Indexes...>> {
   static constexpr std::size_t value = (std::size_t{0} + ... + (is_primary_index_v<Indexes> ? 1U : 0U));
};

template <typename Object, typename Indexes>
struct indexes_match_object;

template <typename Object, typename... Indexes>
struct indexes_match_object<Object, indexed_by<Indexes...>> {
   template <typename Index>
   static constexpr bool matches_index() {
      if constexpr (is_primary_index_v<Index>) {
         return true;
      } else {
         return std::same_as<typename Index::owner_type, typename Object::value_type>;
      }
   }

   static constexpr bool value = (... && matches_index<Indexes>());
};

template <typename Indexes>
struct first_primary_index;

template <typename First, typename... Rest>
struct first_primary_index<indexed_by<First, Rest...>> {
   using type = std::conditional_t<is_primary_index_v<First>, First, typename first_primary_index<indexed_by<Rest...>>::type>;
};

template <>
struct first_primary_index<indexed_by<>> {
   using type = void;
};

template <typename Object>
using primary_index_t = typename first_primary_index<typename Object::indexes_type>::type;

template <typename Object>
using primary_id_t = typename Object::id_type;

template <typename Indexes>
struct unique_tags;

template <>
struct unique_tags<indexed_by<>> : std::true_type {};

template <typename First, typename... Rest>
struct unique_tags<indexed_by<First, Rest...>>
    : std::bool_constant<(!std::same_as<typename First::tag_type, typename Rest::tag_type> && ...) &&
                         unique_tags<indexed_by<Rest...>>::value> {};

template <typename Object, bool HasShape>
struct valid_object_impl : std::false_type {};

template <typename Object>
struct valid_object_impl<Object, true> {
 private:
   static constexpr bool indexed = is_indexed_by_v<typename Object::indexes_type>;
   static constexpr bool one_primary = indexed && primary_count<typename Object::indexes_type>::value == 1;
   static constexpr bool owner_match = indexed && indexes_match_object<Object, typename Object::indexes_type>::value;
   static constexpr bool tags_unique = indexed && unique_tags<typename Object::indexes_type>::value;
   static constexpr bool value_has_base = object_value<typename Object::value_type>;
   static constexpr bool primary_is_typed = typed_id_traits<std::remove_cvref_t<primary_id_t<Object>>>::is_typed_id;

 public:
   static constexpr bool value = indexed && one_primary && owner_match && tags_unique && value_has_base && primary_is_typed;
};

template <typename Object>
struct valid_object
    : valid_object_impl<Object,
                        requires {
                           typename Object::value_type;
                           typename Object::id_type;
                           typename Object::indexes_type;
                        }> {};

template <typename Tag, std::size_t Position, typename... Indexes>
struct find_index_by_tag_impl;

template <typename Tag, std::size_t Position, typename First, typename... Rest>
struct find_index_by_tag_impl<Tag, Position, First, Rest...> {
   using next = find_index_by_tag_impl<Tag, Position + 1, Rest...>;
   using type = std::conditional_t<std::same_as<Tag, typename First::tag_type>, First, typename next::type>;
   static constexpr std::size_t position = std::same_as<Tag, typename First::tag_type> ? Position : next::position;
   static constexpr bool found = std::same_as<Tag, typename First::tag_type> || next::found;
};

template <typename Tag, std::size_t Position>
struct find_index_by_tag_impl<Tag, Position> {
   using type = void;
   static constexpr std::size_t position = Position;
   static constexpr bool found = false;
};

} // namespace detail

template <typename T>
concept object_model = detail::valid_object<T>::value;

template <object_model Object>
using id_type_of = detail::primary_id_t<Object>;

template <object_model Object>
struct object_type_of {
 private:
   using id_type = std::remove_cvref_t<id_type_of<Object>>;

 public:
   static constexpr object_type value{
      .space = typed_id_traits<id_type>::space,
      .type = typed_id_traits<id_type>::type,
   };
};

template <typename Object, typename Tag>
struct index_lookup;

template <typename Value, typename... Indexes, typename Tag>
struct index_lookup<object_index<Value, indexed_by<Indexes...>>, Tag> {
 private:
   using impl = detail::find_index_by_tag_impl<Tag, 0, Indexes...>;
   static_assert(impl::found, "forge::objectdb index tag is not registered for this object");

 public:
   using type = typename impl::type;
   static constexpr std::size_t position = impl::position;
};

template <object_model Object, typename Tag>
using index_by_tag = typename index_lookup<Object, Tag>::type;

template <object_model Object, typename Tag>
inline constexpr index_id index_id_by_tag{.value = static_cast<std::uint32_t>(index_lookup<Object, Tag>::position)};

template <typename Id>
struct object_index_for_id;

template <typename Id>
using object_index_for_id_t = typename object_index_for_id<std::remove_cvref_t<Id>>::type;

} // namespace forge::objectdb
