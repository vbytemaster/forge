module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.objectdb.index;

import forge.ids.object_id;
import forge.objectdb.cursor;
import forge.objectdb.exceptions;
import forge.objectdb.object;
import forge.objectdb.record;

export namespace forge::objectdb {

template <typename T>
struct object_page {
   std::vector<T> items;
   std::optional<cursor> next;
};

struct stream_options {
   std::uint32_t page_size = default_page_limit;
};

enum class index_kind : std::uint8_t {
   primary_unique = 1,
   secondary_unique = 2,
   secondary_non_unique = 3,
};

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

} // namespace forge::objectdb

namespace forge::objectdb::detail {

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
   using type =
      std::conditional_t<is_primary_index_v<First>, First, typename first_primary_index<indexed_by<Rest...>>::type>;
};

template <>
struct first_primary_index<indexed_by<>> {
   using type = void;
};

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
   static constexpr bool primary_is_typed =
      forge::ids::typed_id_traits<std::remove_cvref_t<primary_id_t<Object>>>::is_typed_id;

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

} // namespace forge::objectdb::detail

export namespace forge::objectdb {

template <typename T>
concept object_model = detail::valid_object<T>::value;

template <object_model Object>
using id_type_of = detail::primary_id_t<Object>;

template <object_model Object>
struct object_id_of {
 private:
   using id_type = std::remove_cvref_t<id_type_of<Object>>;

 public:
   static constexpr std::uint8_t space = forge::ids::typed_id_traits<id_type>::space;
   static constexpr std::uint16_t type = forge::ids::typed_id_traits<id_type>::type;
   static constexpr forge::ids::object_id value{.space = space, .type = type, .instance = 0};
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
inline constexpr std::uint32_t index_id_by_tag = static_cast<std::uint32_t>(index_lookup<Object, Tag>::position);

template <object_model Object, typename Tag>
class index_view;

} // namespace forge::objectdb

namespace forge::objectdb::detail {

enum class entry_kind : std::uint8_t {
   secondary_unique_index = 0x20,
   secondary_non_unique_index = 0x21,
};

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
   } else if constexpr (forge::ids::typed_id_traits<value_type>::is_typed_id) {
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

inline void append_record_prefix(std::vector<std::byte>& out, entry_kind kind, forge::ids::object_id type) {
   append_byte(out, static_cast<std::uint8_t>(kind));
   append_byte(out, type.space);
   append_be16(out, type.type);
}

inline void append_secondary_prefix(std::vector<std::byte>& out,
                                    entry_kind kind,
                                    forge::ids::object_id type,
                                    std::uint32_t ordinal) {
   append_record_prefix(out, kind, type);
   append_be32(out, ordinal);
}

inline record_range prefix_range(std::vector<std::byte> prefix) {
   auto scan_prefix = prefix;
   auto end = prefix;
   for (auto index = end.size(); index > 0; --index) {
      auto value = static_cast<unsigned>(end[index - 1U]);
      if (value != 0xffU) {
         end[index - 1U] = static_cast<std::byte>(value + 1U);
         end.resize(index);
         return record_range{
            .begin = record_key{std::move(prefix)},
            .end = record_key{std::move(end)},
            .prefix = record_key{std::move(scan_prefix)},
            .has_end = true};
      }
   }
   return record_range{
      .begin = record_key{std::move(prefix)},
      .end = record_key{},
      .prefix = record_key{std::move(scan_prefix)},
      .has_end = false};
}

template <object_model Object, typename Tag, typename... PrefixValues>
[[nodiscard]] record_range range_from_prefix(const PrefixValues&... values) {
   using index = index_by_tag<Object, Tag>;
   static_assert(secondary_index<index>, "objectdb range_from_prefix is only valid for secondary indexes");

   auto bytes = std::vector<std::byte>{};
   constexpr auto kind = index::kind == index_kind::secondary_unique ? entry_kind::secondary_unique_index
                                                                     : entry_kind::secondary_non_unique_index;
   append_secondary_prefix(bytes, kind, object_id_of<Object>::value, index_id_by_tag<Object, Tag>);
   key_encoder<typename index::key_spec>::append_prefix(bytes, values...);
   return prefix_range(std::move(bytes));
}

template <object_model Object, typename Tag, typename... PrefixValues>
[[nodiscard]] record_range range_from_prefix(const std::tuple<PrefixValues...>& values) {
   using index = index_by_tag<Object, Tag>;
   static_assert(secondary_index<index>, "objectdb range_from_prefix is only valid for secondary indexes");
   static_assert(sizeof...(PrefixValues) <= index::key_spec::size,
                 "objectdb tuple prefix is longer than the index key");

   auto bytes = std::vector<std::byte>{};
   constexpr auto kind = index::kind == index_kind::secondary_unique ? entry_kind::secondary_unique_index
                                                                     : entry_kind::secondary_non_unique_index;
   append_secondary_prefix(bytes, kind, object_id_of<Object>::value, index_id_by_tag<Object, Tag>);
   append_tuple_prefix(bytes, values);
   return prefix_range(std::move(bytes));
}

template <object_model Object, typename Tag>
[[nodiscard]] record_range range_for_index() {
   using index = index_by_tag<Object, Tag>;
   static_assert(secondary_index<index>, "objectdb range_for_index is only valid for secondary indexes");

   auto bytes = std::vector<std::byte>{};
   constexpr auto kind = index::kind == index_kind::secondary_unique ? entry_kind::secondary_unique_index
                                                                     : entry_kind::secondary_non_unique_index;
   append_secondary_prefix(bytes, kind, object_id_of<Object>::value, index_id_by_tag<Object, Tag>);
   return prefix_range(std::move(bytes));
}

} // namespace forge::objectdb::detail

export namespace forge::objectdb {

template <typename T>
using index_page_query = std::function<boost::asio::awaitable<object_page<T>>(record_range, page_request)>;

template <typename T>
using index_stream_query_factory = std::function<index_page_query<T>()>;

template <typename T>
class index_stream {
 public:
   index_stream() = default;

   index_stream(index_page_query<T> query, record_range range, stream_options options)
       : query_{std::move(query)}, range_{std::move(range)}, page_size_{options.page_size} {}

   boost::asio::awaitable<std::optional<T>> next() {
      validate_page_request(page_request{.limit = page_size_});
      if (offset_ < current_.items.size()) {
         co_return current_.items[offset_++];
      }
      if (exhausted_) {
         co_return std::nullopt;
      }

      current_ = co_await query_(range_, page_request{.after = std::move(current_.next), .limit = page_size_});
      offset_ = 0;
      if (current_.items.empty()) {
         exhausted_ = !current_.next.has_value();
         co_return std::nullopt;
      }
      if (!current_.next.has_value()) {
         exhausted_ = true;
      }
      co_return current_.items[offset_++];
   }

 private:
   index_page_query<T> query_;
   record_range range_;
   object_page<T> current_;
   std::size_t offset_ = 0;
   std::uint32_t page_size_ = default_page_limit;
   bool exhausted_ = false;
};

template <object_model Object, typename Tag>
class range_query {
 public:
   using value_type = typename Object::value_type;

   range_query() = default;

   range_query(index_page_query<value_type> page, index_stream_query_factory<value_type> stream_page, record_range range)
       : page_{std::move(page)}, stream_page_{std::move(stream_page)}, range_{std::move(range)} {}

   boost::asio::awaitable<object_page<value_type>> page(page_request request = {}) {
      co_return co_await page_(range_, std::move(request));
   }

   [[nodiscard]] index_stream<value_type> stream(stream_options options = {}) {
      auto query = stream_page_ ? stream_page_() : page_;
      return index_stream<value_type>{std::move(query), range_, options};
   }

   template <typename Fn>
   boost::asio::awaitable<void> for_each(stream_options options, Fn&& fn) {
      auto values = stream(options);
      while (auto value = co_await values.next()) {
         co_await std::invoke(fn, *value);
      }
      co_return;
   }

 private:
   index_page_query<value_type> page_;
   index_stream_query_factory<value_type> stream_page_;
   record_range range_;
};

template <object_model Object, typename Tag>
class index_view {
 public:
   using value_type = typename Object::value_type;

   index_view() = default;

   explicit index_view(index_page_query<value_type> page, index_stream_query_factory<value_type> stream_page = {})
       : page_{std::move(page)}, stream_page_{std::move(stream_page)} {}

   boost::asio::awaitable<object_page<value_type>> page(record_range range, page_request request) {
      co_return co_await page_(std::move(range), std::move(request));
   }

   template <typename Key>
   boost::asio::awaitable<std::optional<value_type>> find(const Key& key) {
      auto result = co_await equal_range(std::tuple{key}).page(page_request{.limit = 1});
      if (result.items.empty()) {
         co_return std::nullopt;
      }
      co_return result.items.front();
   }

   template <typename... PrefixValues>
   [[nodiscard]] range_query<Object, Tag> equal_range(const std::tuple<PrefixValues...>& prefix) const {
      return range_query<Object, Tag>{page_, stream_page_, detail::range_from_prefix<Object, Tag>(prefix)};
   }

   template <typename... PrefixValues>
   [[nodiscard]] range_query<Object, Tag> equal_range(const PrefixValues&... values) const {
      return equal_range(std::make_tuple(values...));
   }

   template <typename... PrefixValues>
   [[nodiscard]] range_query<Object, Tag> lower_bound(const std::tuple<PrefixValues...>& prefix) const {
      auto range = detail::range_for_index<Object, Tag>();
      range.begin = detail::range_from_prefix<Object, Tag>(prefix).begin;
      return range_query<Object, Tag>{page_, stream_page_, std::move(range)};
   }

   template <typename... PrefixValues>
   [[nodiscard]] range_query<Object, Tag> upper_bound(const std::tuple<PrefixValues...>& prefix) const {
      auto range = detail::range_for_index<Object, Tag>();
      auto exact = detail::range_from_prefix<Object, Tag>(prefix);
      range.begin = exact.has_end ? std::move(exact.end) : std::move(exact.begin);
      return range_query<Object, Tag>{page_, stream_page_, std::move(range)};
   }

 private:
   index_page_query<value_type> page_;
   index_stream_query_factory<value_type> stream_page_;
};

} // namespace forge::objectdb
