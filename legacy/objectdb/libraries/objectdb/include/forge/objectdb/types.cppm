module;

#include <cstddef>
#include <cstdint>
#include <compare>
#include <string>
#include <utility>
#include <vector>

export module forge.objectdb.types;

export namespace forge::objectdb {

using byte_vector = std::vector<std::byte>;

struct table_id {
   std::uint32_t value = 0;

   friend constexpr bool operator==(table_id, table_id) noexcept = default;
   friend constexpr auto operator<=>(table_id, table_id) noexcept = default;
};

struct index_id {
   std::uint32_t value = 0;

   friend constexpr bool operator==(index_id, index_id) noexcept = default;
   friend constexpr auto operator<=>(index_id, index_id) noexcept = default;
};

struct object_id {
   std::uint64_t value = 0;

   friend constexpr bool operator==(object_id, object_id) noexcept = default;
   friend constexpr auto operator<=>(object_id, object_id) noexcept = default;
};

enum class index_kind {
   primary,
   secondary_unique,
   secondary_non_unique,
};

struct table_descriptor {
   table_id id;
   std::string name;
};

struct index_descriptor {
   index_id id;
   std::string name;
   index_kind kind = index_kind::secondary_non_unique;
};

template <std::uint32_t Value> struct table {
   static constexpr table_id id{Value};
};

template <std::uint32_t Value, index_kind Kind = index_kind::secondary_non_unique> struct index {
   static constexpr index_id id{Value};
   static constexpr index_kind kind = Kind;
};

template <typename Object, typename Table, typename... Indexes> struct schema {
   using object_type = Object;
   using table_type = Table;

   static constexpr table_id table = Table::id;
   static constexpr std::size_t index_count = sizeof...(Indexes);
};

} // namespace forge::objectdb
