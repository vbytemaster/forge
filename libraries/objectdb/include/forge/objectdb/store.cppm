module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <typeindex>
#include <utility>
#include <vector>

export module forge.objectdb.store;

import forge.ids.types;
import forge.objectdb.cursor;
import forge.objectdb.descriptor;
import forge.objectdb.exceptions;
import forge.objectdb.layout;
import forge.objectdb.types;
import forge.raw.raw;

namespace forge::objectdb::detail {

template <typename T>
std::vector<std::byte> to_byte_vector(const std::vector<T>& input) {
   auto out = std::vector<std::byte>{};
   out.reserve(input.size());
   for (auto value : input) {
      out.push_back(static_cast<std::byte>(value));
   }
   return out;
}

inline std::vector<std::uint8_t> to_uint8_vector(const std::vector<std::byte>& input) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(input.size());
   for (auto value : input) {
      out.push_back(static_cast<std::uint8_t>(value));
   }
   return out;
}

template <typename T>
std::vector<std::byte> pack_value(const T& value) {
   auto bytes = std::vector<std::uint8_t>{};
   forge::raw::pack(bytes, value);
   return to_byte_vector(bytes);
}

template <typename T>
T unpack_value(const std::vector<std::byte>& bytes) {
   return forge::raw::unpack<T>(to_uint8_vector(bytes));
}

struct storage_session_concept {
   virtual ~storage_session_concept() = default;

   virtual boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(record_key key) = 0;
   virtual boost::asio::awaitable<void> put(record_key key, std::vector<std::byte> value) = 0;
   virtual boost::asio::awaitable<void> erase(record_key key) = 0;
   virtual boost::asio::awaitable<record_scan_result> scan_page(key_range range, page_request request) = 0;
   virtual boost::asio::awaitable<void> commit() = 0;
   virtual boost::asio::awaitable<void> rollback() = 0;
};

template <typename NativeSession>
struct storage_session_model final : storage_session_concept {
   explicit storage_session_model(NativeSession value) : native(std::move(value)) {}

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(record_key key) override {
      co_return co_await native.get(std::move(key));
   }

   boost::asio::awaitable<void> put(record_key key, std::vector<std::byte> value) override {
      co_await native.put(std::move(key), std::move(value));
      co_return;
   }

   boost::asio::awaitable<void> erase(record_key key) override {
      co_await native.erase(std::move(key));
      co_return;
   }

   boost::asio::awaitable<record_scan_result> scan_page(key_range range, page_request request) override {
      co_return co_await native.scan_page(std::move(range), std::move(request));
   }

   boost::asio::awaitable<void> commit() override {
      co_await native.commit();
      co_return;
   }

   boost::asio::awaitable<void> rollback() override {
      co_await native.rollback();
      co_return;
   }

   NativeSession native;
};

struct storage_concept {
   virtual ~storage_concept() = default;
   virtual boost::asio::awaitable<std::shared_ptr<storage_session_concept>> session() = 0;
};

template <typename Storage>
struct storage_model final : storage_concept {
   explicit storage_model(Storage value) : native(std::move(value)) {}

   boost::asio::awaitable<std::shared_ptr<storage_session_concept>> session() override {
      auto native_session = co_await native.session();
      co_return std::make_shared<storage_session_model<decltype(native_session)>>(std::move(native_session));
   }

   Storage native;
};

struct store_state {
   std::map<object_type, std::type_index> registered;
};

template <object_model Object, std::size_t Index = 0>
boost::asio::awaitable<void> check_unique_indexes(storage_session_concept& storage,
                                                  const typename Object::value_type& value) {
   using indexes = typename Object::indexes_type::tuple_type;
   if constexpr (Index < std::tuple_size_v<indexes>) {
      using index = std::tuple_element_t<Index, indexes>;
      if constexpr (index::kind == index_kind::secondary_unique) {
         const auto key = layout<Object>::template index_entry_key<typename index::tag_type>(value);
         const auto existing = co_await storage.get(key);
         if (existing.has_value()) {
            const auto existing_id = unpack_value<id_type_of<Object>>(*existing);
            if (existing_id != value.id) {
               FORGE_THROW_EXCEPTION(exceptions::duplicate_object, "objectdb unique index value already exists");
            }
         }
      }
      co_await check_unique_indexes<Object, Index + 1>(storage, value);
   }
   co_return;
}

template <object_model Object, std::size_t Index = 0>
boost::asio::awaitable<void> put_secondary_indexes(storage_session_concept& storage,
                                                   const typename Object::value_type& value) {
   using indexes = typename Object::indexes_type::tuple_type;
   if constexpr (Index < std::tuple_size_v<indexes>) {
      using index = std::tuple_element_t<Index, indexes>;
      if constexpr (secondary_index<index>) {
         const auto key = layout<Object>::template index_entry_key<typename index::tag_type>(value);
         co_await storage.put(key, pack_value(value.id));
      }
      co_await put_secondary_indexes<Object, Index + 1>(storage, value);
   }
   co_return;
}

template <object_model Object, std::size_t Index = 0>
boost::asio::awaitable<void> erase_secondary_indexes(storage_session_concept& storage,
                                                     const typename Object::value_type& value) {
   using indexes = typename Object::indexes_type::tuple_type;
   if constexpr (Index < std::tuple_size_v<indexes>) {
      using index = std::tuple_element_t<Index, indexes>;
      if constexpr (secondary_index<index>) {
         const auto key = layout<Object>::template index_entry_key<typename index::tag_type>(value);
         co_await storage.erase(key);
      }
      co_await erase_secondary_indexes<Object, Index + 1>(storage, value);
   }
   co_return;
}

template <object_model Object>
id_type_of<Object> typed_id_from(forge::ids::object_id id) {
   if (!forge::ids::matches<id_type_of<Object>::space, id_type_of<Object>::type>(id)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "object_id does not match objectdb object type");
   }
   return id_type_of<Object>{id};
}

} // namespace forge::objectdb::detail

export namespace forge::objectdb {

template <typename T>
struct object_page {
   std::vector<T> items;
   std::optional<cursor> next;
};

struct stream_options {
   std::uint32_t page_size = default_page_limit;
};

class session;

template <object_model Object, typename Tag>
class range_query;

template <object_model Object, typename Tag>
class index_view;

class store {
 public:
   template <typename Storage>
   explicit store(Storage storage)
       : storage_{std::make_shared<detail::storage_model<Storage>>(std::move(storage))},
         state_{std::make_shared<detail::store_state>()} {}

   template <object_model Object>
   void register_object() {
      const auto type = object_type_of<Object>::value;
      if (state_->registered.contains(type)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "objectdb object type is already registered");
      }
      state_->registered.emplace(type, std::type_index{typeid(Object)});
   }

   boost::asio::awaitable<session> session();

 private:
   friend class session;

   std::shared_ptr<detail::storage_concept> storage_;
   std::shared_ptr<detail::store_state> state_;
};

class session {
 public:
   session() = default;

   session(std::shared_ptr<detail::storage_session_concept> storage, std::shared_ptr<detail::store_state> state)
       : storage_{std::move(storage)}, state_{std::move(state)} {}

   template <object_model Object>
   void ensure_registered() const {
      const auto type = object_type_of<Object>::value;
      const auto found = state_->registered.find(type);
      if (found == state_->registered.end() || found->second != std::type_index{typeid(Object)}) {
         FORGE_THROW_EXCEPTION(exceptions::unregistered_object, "objectdb object type is not registered");
      }
   }

   template <typed_object_id Id>
   boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> get(Id id) {
      co_return co_await get<object_index_for_id_t<Id>>(id.as_object_id());
   }

   template <typed_object_id Id>
   boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> find(Id id) {
      co_return co_await find<object_index_for_id_t<Id>>(id.as_object_id());
   }

   template <typed_object_id Id>
   boost::asio::awaitable<void> erase(Id id) {
      co_await erase<object_index_for_id_t<Id>>(id.as_object_id());
      co_return;
   }

   template <object_model Object>
   boost::asio::awaitable<typename Object::value_type> get(forge::ids::object_id id) {
      const auto value = co_await find<Object>(id);
      if (!value.has_value()) {
         FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb object was not found");
      }
      co_return *value;
   }

   template <object_model Object>
   boost::asio::awaitable<std::optional<typename Object::value_type>> find(forge::ids::object_id id) {
      ensure_registered<Object>();
      const auto typed = detail::typed_id_from<Object>(id);
      const auto key = layout<Object>::object_record_key(typed);
      const auto bytes = co_await storage_->get(key);
      if (!bytes.has_value()) {
         co_return std::nullopt;
      }
      co_return detail::unpack_value<typename Object::value_type>(*bytes);
   }

   template <object_value Value>
   boost::asio::awaitable<void> insert(Value value) {
      using object_model_type = object_index_for_id_t<typename Value::id_type>;
      ensure_registered<object_model_type>();

      const auto object_key = layout<object_model_type>::object_record_key(value.id);
      if ((co_await storage_->get(object_key)).has_value()) {
         FORGE_THROW_EXCEPTION(exceptions::duplicate_object, "objectdb object id already exists");
      }

      co_await detail::check_unique_indexes<object_model_type>(*storage_, value);
      co_await storage_->put(object_key, detail::pack_value(value));
      co_await detail::put_secondary_indexes<object_model_type>(*storage_, value);
      co_await storage_->commit();
      co_return;
   }

   template <object_value Value>
   boost::asio::awaitable<void> replace(Value value) {
      using object_model_type = object_index_for_id_t<typename Value::id_type>;
      ensure_registered<object_model_type>();

      const auto existing = co_await find<object_model_type>(value.id.as_object_id());
      if (!existing.has_value()) {
         FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb object was not found");
      }

      co_await detail::check_unique_indexes<object_model_type>(*storage_, value);
      co_await detail::erase_secondary_indexes<object_model_type>(*storage_, *existing);
      co_await storage_->put(layout<object_model_type>::object_record_key(value.id), detail::pack_value(value));
      co_await detail::put_secondary_indexes<object_model_type>(*storage_, value);
      co_await storage_->commit();
      co_return;
   }

   template <object_model Object>
   boost::asio::awaitable<void> erase(forge::ids::object_id id) {
      ensure_registered<Object>();
      const auto typed = detail::typed_id_from<Object>(id);
      const auto existing = co_await find<Object>(typed.as_object_id());
      if (!existing.has_value()) {
         co_return;
      }

      co_await detail::erase_secondary_indexes<Object>(*storage_, *existing);
      co_await storage_->erase(layout<Object>::object_record_key(typed));
      co_await storage_->commit();
      co_return;
   }

   template <object_model Object, typename Tag>
   [[nodiscard]] index_view<Object, Tag> index() const {
      ensure_registered<Object>();
      return index_view<Object, Tag>{*this};
   }

 private:
   template <object_model Object, typename Tag>
   friend class range_query;

   template <object_model Object, typename Tag>
   friend class index_view;

   std::shared_ptr<detail::storage_session_concept> storage_;
   std::shared_ptr<detail::store_state> state_;
};

inline boost::asio::awaitable<session> store::session() {
   auto native_session = co_await storage_->session();
   co_return forge::objectdb::session{std::move(native_session), state_};
}

template <typename T>
class index_stream {
 public:
   index_stream() = default;

   template <object_model Object, typename Tag>
   index_stream(range_query<Object, Tag> query, stream_options options)
       : query_{
            [query = std::move(query), options](std::optional<cursor> after) mutable
               -> boost::asio::awaitable<object_page<T>> {
               co_return co_await query.page(page_request{.after = std::move(after), .limit = options.page_size});
            }},
         page_size_{options.page_size} {}

   boost::asio::awaitable<std::optional<T>> next() {
      validate_page_request(page_request{.limit = page_size_});
      if (offset_ < current_.items.size()) {
         co_return current_.items[offset_++];
      }
      if (exhausted_) {
         co_return std::nullopt;
      }

      current_ = co_await query_(std::move(current_.next));
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
   std::function<boost::asio::awaitable<object_page<T>>(std::optional<cursor>)> query_;
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

   range_query(session owner, key_range range) : owner_{std::move(owner)}, range_{std::move(range)} {}

   boost::asio::awaitable<object_page<value_type>> page(page_request request = {}) {
      owner_.template ensure_registered<Object>();
      validate_page_request(request);

      auto records = co_await owner_.storage_->scan_page(range_, request);
      auto out = object_page<value_type>{};
      out.next = std::move(records.next).transform([](record_key key) { return cursor{.boundary = std::move(key)}; });

      for (const auto& entry : records.entries) {
         const auto id = detail::unpack_value<id_type_of<Object>>(entry.value);
         auto value = co_await owner_.template find<Object>(id.as_object_id());
         if (!value.has_value()) {
            FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb index points to a missing object");
         }
         out.items.push_back(std::move(*value));
      }

      co_return out;
   }

   [[nodiscard]] index_stream<value_type> stream(stream_options options = {}) {
      return index_stream<value_type>{*this, options};
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
   session owner_;
   key_range range_;
};

template <object_model Object, typename Tag>
class index_view {
 public:
   using value_type = typename Object::value_type;

   explicit index_view(session owner) : owner_{std::move(owner)} {}

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
      return range_query<Object, Tag>{owner_, layout<Object>::template index_prefix<Tag>(prefix)};
   }

   template <typename... PrefixValues>
   [[nodiscard]] range_query<Object, Tag> equal_range(const PrefixValues&... values) const {
      return equal_range(std::make_tuple(values...));
   }

   template <typename... PrefixValues>
   [[nodiscard]] range_query<Object, Tag> lower_bound(const std::tuple<PrefixValues...>& prefix) const {
      auto range = layout<Object>::template index_range<Tag>();
      range.begin = layout<Object>::template index_prefix<Tag>(prefix).begin;
      return range_query<Object, Tag>{owner_, std::move(range)};
   }

   template <typename... PrefixValues>
   [[nodiscard]] range_query<Object, Tag> upper_bound(const std::tuple<PrefixValues...>& prefix) const {
      auto range = layout<Object>::template index_range<Tag>();
      auto exact = layout<Object>::template index_prefix<Tag>(prefix);
      range.begin = exact.has_end ? std::move(exact.end) : std::move(exact.begin);
      return range_query<Object, Tag>{owner_, std::move(range)};
   }

 private:
   session owner_;
};

} // namespace forge::objectdb
