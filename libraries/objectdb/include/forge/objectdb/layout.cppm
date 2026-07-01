module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <vector>

export module forge.objectdb.layout;

import forge.ids.types;
import forge.objectdb.descriptor;
import forge.objectdb.types;

namespace forge::objectdb::detail {

inline void append_byte(std::vector<std::byte>& out, std::uint8_t value) {
   out.push_back(static_cast<std::byte>(value));
}

inline void append_be16(std::vector<std::byte>& out, std::uint16_t value) {
   append_byte(out, static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>(value & 0xffU));
}

inline void append_be32(std::vector<std::byte>& out, std::uint32_t value) {
   append_byte(out, static_cast<std::uint8_t>((value >> 24U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>((value >> 16U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>(value & 0xffU));
}

inline void append_be64(std::vector<std::byte>& out, std::uint64_t value) {
   for (auto shift = 56; shift >= 0; shift -= 8) {
      append_byte(out, static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xffU));
   }
}

template <typename Unsigned>
void append_unsigned(std::vector<std::byte>& out, Unsigned value) {
   static_assert(std::is_unsigned_v<Unsigned>);
   for (auto index = sizeof(Unsigned); index > 0; --index) {
      const auto shift = static_cast<unsigned>((index - 1U) * 8U);
      append_byte(out, static_cast<std::uint8_t>((value >> shift) & static_cast<Unsigned>(0xffU)));
   }
}

template <typename Signed>
void append_signed(std::vector<std::byte>& out, Signed value) {
   static_assert(std::is_signed_v<Signed>);
   using unsigned_type = std::make_unsigned_t<Signed>;
   auto encoded = static_cast<unsigned_type>(value);
   encoded ^= (unsigned_type{1} << (std::numeric_limits<unsigned_type>::digits - 1U));
   append_unsigned(out, encoded);
}

inline void append_string(std::vector<std::byte>& out, std::string_view value) {
   for (unsigned char ch : value) {
      append_byte(out, ch);
      if (ch == 0U) {
         append_byte(out, 0xffU);
      }
   }
   append_byte(out, 0U);
}

inline void append_object_id(std::vector<std::byte>& out, forge::ids::object_id value) {
   append_byte(out, value.space);
   append_be16(out, value.type);
   append_be64(out, value.instance);
}

template <typename T>
void append_value(std::vector<std::byte>& out, const T& value) {
   using value_type = std::remove_cvref_t<T>;
   if constexpr (std::is_same_v<value_type, bool>) {
      append_byte(out, value ? 1U : 0U);
   } else if constexpr (std::is_integral_v<value_type> && std::is_unsigned_v<value_type>) {
      append_unsigned(out, value);
   } else if constexpr (std::is_integral_v<value_type> && std::is_signed_v<value_type>) {
      append_signed(out, value);
   } else if constexpr (std::is_enum_v<value_type>) {
      append_value(out, static_cast<std::underlying_type_t<value_type>>(value));
   } else if constexpr (std::is_same_v<value_type, std::string>) {
      append_string(out, value);
   } else if constexpr (std::is_same_v<value_type, std::string_view>) {
      append_string(out, value);
   } else if constexpr (std::is_convertible_v<T, std::string_view>) {
      append_string(out, std::string_view{value});
   } else if constexpr (std::is_same_v<value_type, forge::ids::object_id>) {
      append_object_id(out, value);
   } else if constexpr (typed_id_traits<value_type>::is_typed_id) {
      append_object_id(out, value.as_object_id());
   } else {
      static_assert(sizeof(value_type) == 0, "forge::objectdb cannot encode this key member type");
   }
}

template <auto Extractor, typename Value>
void append_extracted(std::vector<std::byte>& out, const Value& value) {
   append_value(out, extractor_traits<Extractor>::get(value));
}

template <typename KeySpec>
struct key_encoder;

template <>
struct key_encoder<primary_key> {
   template <typename Value>
   static void append_object(std::vector<std::byte>& out, const Value& value) {
      append_value(out, value.id);
   }

   template <typename PrefixValue>
   static void append_prefix(std::vector<std::byte>& out, const PrefixValue& value) {
      append_value(out, value);
   }
};

template <auto Extractor>
struct key_encoder<member_key<Extractor>> {
   template <typename Value>
   static void append_object(std::vector<std::byte>& out, const Value& value) {
      append_extracted<Extractor>(out, value);
   }

   template <typename PrefixValue>
   static void append_prefix(std::vector<std::byte>& out, const PrefixValue& value) {
      append_value(out, value);
   }
};

template <auto... Extractors>
struct key_encoder<composite_key<Extractors...>> {
   template <typename Value>
   static void append_object(std::vector<std::byte>& out, const Value& value) {
      (append_extracted<Extractors>(out, value), ...);
   }

   template <typename... PrefixValues>
   static void append_prefix(std::vector<std::byte>& out, const PrefixValues&... values) {
      static_assert(sizeof...(PrefixValues) <= sizeof...(Extractors),
                    "objectdb composite prefix is longer than the composite key");
      (append_value(out, values), ...);
   }
};

template <typename Tuple, std::size_t... Indexes>
void append_tuple_prefix_impl(std::vector<std::byte>& out, const Tuple& tuple, std::index_sequence<Indexes...>) {
   (append_value(out, std::get<Indexes>(tuple)), ...);
}

template <typename Tuple>
void append_tuple_prefix(std::vector<std::byte>& out, const Tuple& tuple) {
   append_tuple_prefix_impl(out, tuple, std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>{});
}

inline void append_record_prefix(std::vector<std::byte>& out, record_kind kind, object_type type) {
   append_byte(out, static_cast<std::uint8_t>(kind));
   append_byte(out, type.space);
   append_be16(out, type.type);
}

inline void append_index_prefix(std::vector<std::byte>& out, record_kind kind, object_type type, index_id id) {
   append_record_prefix(out, kind, type);
   append_be32(out, id.value);
}

inline key_range prefix_range(std::vector<std::byte> prefix) {
   auto scan_prefix = prefix;
   auto end = prefix;
   for (auto index = end.size(); index > 0; --index) {
      auto value = static_cast<unsigned>(end[index - 1U]);
      if (value != 0xffU) {
         end[index - 1U] = static_cast<std::byte>(value + 1U);
         end.resize(index);
         return key_range{
            .begin = record_key{std::move(prefix)},
            .end = record_key{std::move(end)},
            .prefix = record_key{std::move(scan_prefix)},
            .has_end = true};
      }
   }
   return key_range{
      .begin = record_key{std::move(prefix)},
      .end = record_key{},
      .prefix = record_key{std::move(scan_prefix)},
      .has_end = false};
}

} // namespace forge::objectdb::detail

export namespace forge::objectdb {

template <object_model Object>
struct layout {
   using object_model_type = Object;
   using value_type = typename Object::value_type;
   using id_type = id_type_of<Object>;

   static constexpr object_type type = object_type_of<Object>::value;

   [[nodiscard]] static record_key object_record_key(id_type id) {
      auto bytes = std::vector<std::byte>{};
      detail::append_record_prefix(bytes, record_kind::object_record, type);
      detail::append_be64(bytes, id.instance);
      return record_key{std::move(bytes)};
   }

   [[nodiscard]] static record_key object_record_key(const value_type& value) {
      return object_record_key(value.id);
   }

   template <typename Tag>
   [[nodiscard]] static record_key index_entry_key(const value_type& value) {
      using index = index_by_tag<Object, Tag>;
      static_assert(secondary_index<index>, "objectdb index_entry_key is only valid for secondary indexes");

      auto bytes = std::vector<std::byte>{};
      constexpr auto kind = index::kind == index_kind::secondary_unique ? record_kind::secondary_unique_index
                                                                        : record_kind::secondary_non_unique_index;
      detail::append_index_prefix(bytes, kind, type, index_id_by_tag<Object, Tag>);
      detail::key_encoder<typename index::key_spec>::append_object(bytes, value);
      if constexpr (index::kind == index_kind::secondary_non_unique) {
         detail::append_be64(bytes, object_record_id(value).instance);
      }
      return record_key{std::move(bytes)};
   }

   [[nodiscard]] static id_type object_record_id(const value_type& value) {
      return value.id;
   }

   template <typename Tag, typename... PrefixValues>
   [[nodiscard]] static key_range index_prefix(const PrefixValues&... values) {
      using index = index_by_tag<Object, Tag>;
      static_assert(secondary_index<index>, "objectdb index_prefix is only valid for secondary indexes");

      auto bytes = std::vector<std::byte>{};
      constexpr auto kind = index::kind == index_kind::secondary_unique ? record_kind::secondary_unique_index
                                                                        : record_kind::secondary_non_unique_index;
      detail::append_index_prefix(bytes, kind, type, index_id_by_tag<Object, Tag>);
      detail::key_encoder<typename index::key_spec>::append_prefix(bytes, values...);
      return detail::prefix_range(std::move(bytes));
   }

   template <typename Tag, typename... PrefixValues>
   [[nodiscard]] static key_range index_prefix(const std::tuple<PrefixValues...>& values) {
      using index = index_by_tag<Object, Tag>;
      static_assert(secondary_index<index>, "objectdb index_prefix is only valid for secondary indexes");
      static_assert(sizeof...(PrefixValues) <= index::key_spec::size,
                    "objectdb tuple prefix is longer than the index key");

      auto bytes = std::vector<std::byte>{};
      constexpr auto kind = index::kind == index_kind::secondary_unique ? record_kind::secondary_unique_index
                                                                        : record_kind::secondary_non_unique_index;
      detail::append_index_prefix(bytes, kind, type, index_id_by_tag<Object, Tag>);
      detail::append_tuple_prefix(bytes, values);
      return detail::prefix_range(std::move(bytes));
   }

   template <typename Tag>
   [[nodiscard]] static key_range index_range() {
      using index = index_by_tag<Object, Tag>;
      static_assert(secondary_index<index>, "objectdb index_range is only valid for secondary indexes");

      auto bytes = std::vector<std::byte>{};
      constexpr auto kind = index::kind == index_kind::secondary_unique ? record_kind::secondary_unique_index
                                                                        : record_kind::secondary_non_unique_index;
      detail::append_index_prefix(bytes, kind, type, index_id_by_tag<Object, Tag>);
      return detail::prefix_range(std::move(bytes));
   }
};

} // namespace forge::objectdb
