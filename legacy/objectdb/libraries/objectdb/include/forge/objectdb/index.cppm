module;

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

export module forge.objectdb.index;

import forge.objectdb.types;

export namespace forge::objectdb {

template <typename T>
concept table_tag = requires {
   { T::id } -> std::same_as<const table_id&>;
};

template <typename T>
concept index_tag = requires {
   { T::id } -> std::same_as<const index_id&>;
   { T::kind } -> std::same_as<const index_kind&>;
};

template <typename Schema>
concept object_schema = requires {
   typename Schema::object_type;
   typename Schema::table_type;
   { Schema::table } -> std::same_as<const table_id&>;
   { Schema::index_count } -> std::convertible_to<std::size_t>;
};

template <typename... Indexes> struct index_set {
   using tuple_type = std::tuple<Indexes...>;
   static constexpr std::size_t size = sizeof...(Indexes);
};

template <typename Schema> using object_type_t = typename Schema::object_type;
template <typename Schema> using table_type_t = typename Schema::table_type;

} // namespace forge::objectdb
