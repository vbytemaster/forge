module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <typeindex>
#include <type_traits>
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

enum class mutation_kind : std::uint8_t {
   insert = 1,
   replace = 2,
   modify = 3,
   erase = 4,
};

struct object_mutation {
   mutation_kind kind = mutation_kind::insert;
   object_type type;
   forge::ids::object_id id;
   std::optional<std::vector<std::byte>> before;
   std::optional<std::vector<std::byte>> after;
};

struct change_set {
   std::vector<object_mutation> mutations;

   [[nodiscard]] bool empty() const noexcept {
      return mutations.empty();
   }
};

class interceptor {
 public:
   virtual ~interceptor() = default;
   virtual boost::asio::awaitable<void> before_mutation(const object_mutation& mutation) = 0;
};

class observer {
 public:
   virtual ~observer() = default;
   virtual boost::asio::awaitable<void> after_commit(const change_set& changes) = 0;
};

class session {
 public:
   virtual ~session() = default;

   virtual boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(record_key key) = 0;
   virtual boost::asio::awaitable<void> put(record_key key, std::vector<std::byte> value) = 0;
   virtual boost::asio::awaitable<void> erase(record_key key) = 0;
   virtual boost::asio::awaitable<record_scan_result> scan_page(key_range range, page_request request) = 0;
   virtual boost::asio::awaitable<void> commit() = 0;
   virtual boost::asio::awaitable<void> rollback() = 0;
};

template <typename T>
concept session_model = std::derived_from<T, session>;

template <session_model Session>
class session_factory {
 public:
   using begin_fn = std::function<boost::asio::awaitable<std::unique_ptr<Session>>()>;

   explicit session_factory(begin_fn begin) : begin_{std::move(begin)} {}

 private:
   friend class store;

   boost::asio::awaitable<std::unique_ptr<session>> begin_session() const {
      if (!begin_) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "objectdb session factory is empty");
      }
      auto native = co_await begin_();
      if (!native) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "objectdb session factory returned null session");
      }
      std::unique_ptr<session> erased = std::move(native);
      co_return erased;
   }

   begin_fn begin_;
};

class transaction;

template <object_model Object, typename Tag>
class range_query;

template <object_model Object, typename Tag>
class index_view;

template <object_model Object, typename Tag>
class store_range_query;

template <object_model Object, typename Tag>
class store_index_view;

template <typename T>
class index_stream;

template <object_model Object, typename Tag>
class store_index_stream;

class store {
 public:
   template <session_model Session>
   explicit store(session_factory<Session> factory)
       : impl_{std::make_shared<impl>(
            [factory = std::move(factory)]() mutable -> boost::asio::awaitable<std::unique_ptr<session>> {
               co_return co_await factory.begin_session();
            })} {}

   template <object_model Object>
   void register_object();

   void add_interceptor(std::shared_ptr<interceptor> value);
   void add_observer(std::shared_ptr<observer> value);

   boost::asio::awaitable<transaction> begin_transaction();

   template <typed_object_id Id>
   boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> get(Id id);

   template <typed_object_id Id>
   boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> find(Id id);

   template <object_model Object>
   boost::asio::awaitable<typename Object::value_type> get(forge::ids::object_id id);

   template <object_model Object>
   boost::asio::awaitable<std::optional<typename Object::value_type>> find(forge::ids::object_id id);

   template <object_value Value>
   boost::asio::awaitable<void> insert(Value value);

   template <object_value Value>
   boost::asio::awaitable<void> replace(Value value);

   template <typed_object_id Id, typename Fn>
   boost::asio::awaitable<void> modify(Id id, Fn&& fn);

   template <typed_object_id Id>
   boost::asio::awaitable<void> erase(Id id);

   template <object_model Object>
   boost::asio::awaitable<void> erase(forge::ids::object_id id);

   template <object_model Object, typename Tag>
   [[nodiscard]] store_index_view<Object, Tag> index() const;

 private:
   friend class transaction;

   template <object_model Object, typename Tag>
   friend class store_range_query;

   template <object_model Object, typename Tag>
   friend class store_index_view;

   template <object_model Object, typename Tag>
   friend class store_index_stream;

   struct impl;
   std::shared_ptr<impl> impl_;
};

class transaction {
 public:
   transaction() = default;

   template <typed_object_id Id>
   boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> get(Id id) {
      co_return co_await get<object_index_for_id_t<Id>>(id.as_object_id());
   }

   template <typed_object_id Id>
   boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> find(Id id) {
      co_return co_await find<object_index_for_id_t<Id>>(id.as_object_id());
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
      const auto bytes = co_await active_session().get(key);
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
      if ((co_await active_session().get(object_key)).has_value()) {
         FORGE_THROW_EXCEPTION(exceptions::duplicate_object, "objectdb object id already exists");
      }

      auto after = detail::pack_value(value);
      auto mutation = object_mutation{
         .kind = mutation_kind::insert,
         .type = object_type_of<object_model_type>::value,
         .id = value.id.as_object_id(),
         .after = after,
      };
      co_await before_mutation(mutation);
      co_await check_unique_indexes<object_model_type>(value);
      co_await active_session().put(object_key, std::move(after));
      co_await put_secondary_indexes<object_model_type>(value);
      changes().mutations.push_back(std::move(mutation));
      co_return;
   }

   template <object_value Value>
   boost::asio::awaitable<void> replace(Value value) {
      co_await replace_value(std::move(value), mutation_kind::replace);
      co_return;
   }

   template <typed_object_id Id, typename Fn>
   boost::asio::awaitable<void> modify(Id id, Fn&& fn) {
      using object_model_type = object_index_for_id_t<Id>;
      auto next = co_await get(id);
      using result_type = std::invoke_result_t<Fn&, typename object_model_type::value_type&>;
      static_assert(std::is_void_v<result_type>, "objectdb modify mutator must return void");
      std::invoke(fn, next);
      co_await replace_value(std::move(next), mutation_kind::modify);
      co_return;
   }

   template <typed_object_id Id>
   boost::asio::awaitable<void> erase(Id id) {
      co_await erase<object_index_for_id_t<Id>>(id.as_object_id());
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

      auto before = detail::pack_value(*existing);
      auto mutation = object_mutation{
         .kind = mutation_kind::erase,
         .type = object_type_of<Object>::value,
         .id = typed.as_object_id(),
         .before = before,
      };
      co_await before_mutation(mutation);
      co_await erase_secondary_indexes<Object>(*existing);
      co_await active_session().erase(layout<Object>::object_record_key(typed));
      changes().mutations.push_back(std::move(mutation));
      co_return;
   }

   template <object_model Object, typename Tag>
   [[nodiscard]] index_view<Object, Tag> index() const {
      ensure_registered<Object>();
      return index_view<Object, Tag>{*this};
   }

   boost::asio::awaitable<void> commit();
   boost::asio::awaitable<void> rollback();

 private:
   friend class store;

   template <object_model Object, typename Tag>
   friend class range_query;

   template <object_model Object, typename Tag>
   friend class index_view;

   template <typename T>
   friend class index_stream;

   explicit transaction(std::shared_ptr<store::impl> owner, std::unique_ptr<session> active);

   struct state;

   [[nodiscard]] session& active_session() const;
   [[nodiscard]] change_set& changes() const;

   template <object_model Object>
   void ensure_registered() const;

   boost::asio::awaitable<void> before_mutation(const object_mutation& mutation) const;

   template <object_model Object>
   boost::asio::awaitable<void> check_unique_indexes(const typename Object::value_type& value);

   template <object_model Object, std::size_t Index = 0>
   boost::asio::awaitable<void> check_unique_indexes_impl(const typename Object::value_type& value);

   template <object_model Object>
   boost::asio::awaitable<void> put_secondary_indexes(const typename Object::value_type& value);

   template <object_model Object, std::size_t Index = 0>
   boost::asio::awaitable<void> put_secondary_indexes_impl(const typename Object::value_type& value);

   template <object_model Object>
   boost::asio::awaitable<void> erase_secondary_indexes(const typename Object::value_type& value);

   template <object_model Object, std::size_t Index = 0>
   boost::asio::awaitable<void> erase_secondary_indexes_impl(const typename Object::value_type& value);

   template <object_value Value>
   boost::asio::awaitable<void> replace_value(Value value, mutation_kind kind);

   std::shared_ptr<state> state_;
};

struct store::impl {
   using begin_fn = std::function<boost::asio::awaitable<std::unique_ptr<session>>()>;

   explicit impl(begin_fn begin) : begin_session{std::move(begin)} {}

   boost::asio::awaitable<std::unique_ptr<session>> open_session() const {
      if (!begin_session) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "objectdb store has no session factory");
      }
      auto active = co_await begin_session();
      if (!active) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "objectdb session factory returned null session");
      }
      co_return active;
   }

   template <object_model Object>
   void ensure_registered() const {
      const auto type = object_type_of<Object>::value;
      const auto found = registered.find(type);
      if (found == registered.end() || found->second != std::type_index{typeid(Object)}) {
         FORGE_THROW_EXCEPTION(exceptions::unregistered_object, "objectdb object type is not registered");
      }
   }

   begin_fn begin_session;
   std::map<object_type, std::type_index> registered;
   std::vector<std::shared_ptr<interceptor>> interceptors;
   std::vector<std::shared_ptr<observer>> observers;
};

struct transaction::state {
   std::shared_ptr<store::impl> owner;
   std::unique_ptr<session> active;
   change_set changes;
   bool closed = false;
   bool committed = false;
};

template <object_model Object>
void store::register_object() {
   const auto type = object_type_of<Object>::value;
   if (impl_->registered.contains(type)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "objectdb object type is already registered");
   }
   impl_->registered.emplace(type, std::type_index{typeid(Object)});
}

inline void store::add_interceptor(std::shared_ptr<interceptor> value) {
   if (value) {
      impl_->interceptors.push_back(std::move(value));
   }
}

inline void store::add_observer(std::shared_ptr<observer> value) {
   if (value) {
      impl_->observers.push_back(std::move(value));
   }
}

inline boost::asio::awaitable<transaction> store::begin_transaction() {
   auto active = co_await impl_->open_session();
   co_return transaction{impl_, std::move(active)};
}

inline transaction::transaction(std::shared_ptr<store::impl> owner, std::unique_ptr<session> active)
    : state_{std::make_shared<state>(state{.owner = std::move(owner), .active = std::move(active)})} {}

inline session& transaction::active_session() const {
   if (!state_ || !state_->active || state_->closed) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb transaction is closed");
   }
   return *state_->active;
}

inline change_set& transaction::changes() const {
   if (!state_) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb transaction is closed");
   }
   return state_->changes;
}

template <object_model Object>
void transaction::ensure_registered() const {
   if (!state_ || !state_->owner) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb transaction is closed");
   }
   state_->owner->template ensure_registered<Object>();
}

inline boost::asio::awaitable<void> transaction::before_mutation(const object_mutation& mutation) const {
   for (const auto& hook : state_->owner->interceptors) {
      co_await hook->before_mutation(mutation);
   }
   co_return;
}

template <object_model Object>
boost::asio::awaitable<void> transaction::check_unique_indexes(const typename Object::value_type& value) {
   co_await check_unique_indexes_impl<Object>(value);
   co_return;
}

template <object_model Object, std::size_t Index>
boost::asio::awaitable<void> transaction::check_unique_indexes_impl(const typename Object::value_type& value) {
   using indexes = typename Object::indexes_type::tuple_type;
   if constexpr (Index < std::tuple_size_v<indexes>) {
      using index = std::tuple_element_t<Index, indexes>;
      if constexpr (index::kind == index_kind::secondary_unique) {
         const auto key = layout<Object>::template index_entry_key<typename index::tag_type>(value);
         const auto existing = co_await active_session().get(key);
         if (existing.has_value()) {
            const auto existing_id = detail::unpack_value<id_type_of<Object>>(*existing);
            if (existing_id != value.id) {
               FORGE_THROW_EXCEPTION(exceptions::duplicate_object, "objectdb unique index value already exists");
            }
         }
      }
      co_await check_unique_indexes_impl<Object, Index + 1>(value);
   }
   co_return;
}

template <object_model Object>
boost::asio::awaitable<void> transaction::put_secondary_indexes(const typename Object::value_type& value) {
   co_await put_secondary_indexes_impl<Object>(value);
   co_return;
}

template <object_model Object, std::size_t Index>
boost::asio::awaitable<void> transaction::put_secondary_indexes_impl(const typename Object::value_type& value) {
   using indexes = typename Object::indexes_type::tuple_type;
   if constexpr (Index < std::tuple_size_v<indexes>) {
      using index = std::tuple_element_t<Index, indexes>;
      if constexpr (secondary_index<index>) {
         const auto key = layout<Object>::template index_entry_key<typename index::tag_type>(value);
         co_await active_session().put(key, detail::pack_value(value.id));
      }
      co_await put_secondary_indexes_impl<Object, Index + 1>(value);
   }
   co_return;
}

template <object_model Object>
boost::asio::awaitable<void> transaction::erase_secondary_indexes(const typename Object::value_type& value) {
   co_await erase_secondary_indexes_impl<Object>(value);
   co_return;
}

template <object_model Object, std::size_t Index>
boost::asio::awaitable<void> transaction::erase_secondary_indexes_impl(const typename Object::value_type& value) {
   using indexes = typename Object::indexes_type::tuple_type;
   if constexpr (Index < std::tuple_size_v<indexes>) {
      using index = std::tuple_element_t<Index, indexes>;
      if constexpr (secondary_index<index>) {
         const auto key = layout<Object>::template index_entry_key<typename index::tag_type>(value);
         co_await active_session().erase(key);
      }
      co_await erase_secondary_indexes_impl<Object, Index + 1>(value);
   }
   co_return;
}

template <object_value Value>
boost::asio::awaitable<void> transaction::replace_value(Value value, mutation_kind kind) {
   using object_model_type = object_index_for_id_t<typename Value::id_type>;
   ensure_registered<object_model_type>();

   const auto existing = co_await find<object_model_type>(value.id.as_object_id());
   if (!existing.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb object was not found");
   }

   auto before = detail::pack_value(*existing);
   auto after = detail::pack_value(value);
   auto mutation = object_mutation{
      .kind = kind,
      .type = object_type_of<object_model_type>::value,
      .id = value.id.as_object_id(),
      .before = before,
      .after = after,
   };
   co_await before_mutation(mutation);
   co_await check_unique_indexes<object_model_type>(value);
   co_await erase_secondary_indexes<object_model_type>(*existing);
   co_await active_session().put(layout<object_model_type>::object_record_key(value.id), std::move(after));
   co_await put_secondary_indexes<object_model_type>(value);
   changes().mutations.push_back(std::move(mutation));
   co_return;
}

inline boost::asio::awaitable<void> transaction::commit() {
   auto& current = active_session();
   co_await current.commit();
   state_->committed = true;
   state_->closed = true;
   state_->active.reset();

   if (!state_->changes.empty()) {
      for (const auto& hook : state_->owner->observers) {
         co_await hook->after_commit(state_->changes);
      }
   }
   co_return;
}

inline boost::asio::awaitable<void> transaction::rollback() {
   if (!state_ || !state_->active || state_->closed) {
      co_return;
   }
   co_await state_->active->rollback();
   state_->active.reset();
   state_->closed = true;
   state_->changes.mutations.clear();
   co_return;
}

template <typed_object_id Id>
boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> store::get(Id id) {
   auto tx = co_await begin_transaction();
   auto failure = std::exception_ptr{};
   try {
      auto value = co_await tx.get(id);
      co_await tx.rollback();
      co_return value;
   } catch (...) {
      failure = std::current_exception();
   }
   co_await tx.rollback();
   std::rethrow_exception(failure);
}

template <typed_object_id Id>
boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> store::find(Id id) {
   auto tx = co_await begin_transaction();
   auto failure = std::exception_ptr{};
   try {
      auto value = co_await tx.find(id);
      co_await tx.rollback();
      co_return value;
   } catch (...) {
      failure = std::current_exception();
   }
   co_await tx.rollback();
   std::rethrow_exception(failure);
}

template <object_model Object>
boost::asio::awaitable<typename Object::value_type> store::get(forge::ids::object_id id) {
   auto tx = co_await begin_transaction();
   auto failure = std::exception_ptr{};
   try {
      auto value = co_await tx.template get<Object>(id);
      co_await tx.rollback();
      co_return value;
   } catch (...) {
      failure = std::current_exception();
   }
   co_await tx.rollback();
   std::rethrow_exception(failure);
}

template <object_model Object>
boost::asio::awaitable<std::optional<typename Object::value_type>> store::find(forge::ids::object_id id) {
   auto tx = co_await begin_transaction();
   auto failure = std::exception_ptr{};
   try {
      auto value = co_await tx.template find<Object>(id);
      co_await tx.rollback();
      co_return value;
   } catch (...) {
      failure = std::current_exception();
   }
   co_await tx.rollback();
   std::rethrow_exception(failure);
}

template <object_value Value>
boost::asio::awaitable<void> store::insert(Value value) {
   auto tx = co_await begin_transaction();
   auto failure = std::exception_ptr{};
   try {
      co_await tx.insert(std::move(value));
      co_await tx.commit();
   } catch (...) {
      failure = std::current_exception();
   }
   if (failure) {
      co_await tx.rollback();
      std::rethrow_exception(failure);
   }
   co_return;
}

template <object_value Value>
boost::asio::awaitable<void> store::replace(Value value) {
   auto tx = co_await begin_transaction();
   auto failure = std::exception_ptr{};
   try {
      co_await tx.replace(std::move(value));
      co_await tx.commit();
   } catch (...) {
      failure = std::current_exception();
   }
   if (failure) {
      co_await tx.rollback();
      std::rethrow_exception(failure);
   }
   co_return;
}

template <typed_object_id Id, typename Fn>
boost::asio::awaitable<void> store::modify(Id id, Fn&& fn) {
   auto tx = co_await begin_transaction();
   auto failure = std::exception_ptr{};
   try {
      co_await tx.modify(id, std::forward<Fn>(fn));
      co_await tx.commit();
   } catch (...) {
      failure = std::current_exception();
   }
   if (failure) {
      co_await tx.rollback();
      std::rethrow_exception(failure);
   }
   co_return;
}

template <typed_object_id Id>
boost::asio::awaitable<void> store::erase(Id id) {
   auto tx = co_await begin_transaction();
   auto failure = std::exception_ptr{};
   try {
      co_await tx.erase(id);
      co_await tx.commit();
   } catch (...) {
      failure = std::current_exception();
   }
   if (failure) {
      co_await tx.rollback();
      std::rethrow_exception(failure);
   }
   co_return;
}

template <object_model Object>
boost::asio::awaitable<void> store::erase(forge::ids::object_id id) {
   auto tx = co_await begin_transaction();
   auto failure = std::exception_ptr{};
   try {
      co_await tx.template erase<Object>(id);
      co_await tx.commit();
   } catch (...) {
      failure = std::current_exception();
   }
   if (failure) {
      co_await tx.rollback();
      std::rethrow_exception(failure);
   }
   co_return;
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

   range_query(transaction owner, key_range range) : owner_{std::move(owner)}, range_{std::move(range)} {}

   boost::asio::awaitable<object_page<value_type>> page(page_request request = {}) {
      owner_.template ensure_registered<Object>();
      validate_page_request(request);

      auto records = co_await owner_.active_session().scan_page(range_, request);
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
   transaction owner_;
   key_range range_;
};

template <object_model Object, typename Tag>
class index_view {
 public:
   using value_type = typename Object::value_type;

   explicit index_view(transaction owner) : owner_{std::move(owner)} {}

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
   transaction owner_;
};

template <object_model Object, typename Tag>
class store_index_stream {
 public:
   using value_type = typename Object::value_type;

   store_index_stream() = default;

   store_index_stream(store owner, key_range range, stream_options options)
       : owner_{std::move(owner)}, range_{std::move(range)}, options_{options} {}

   boost::asio::awaitable<std::optional<value_type>> next() {
      if (!active_) {
         auto tx = co_await owner_.begin_transaction();
         auto query = range_query<Object, Tag>{tx, range_};
         active_ = std::move(tx);
         values_ = query.stream(options_);
      }

      auto value = co_await values_->next();
      if (!value.has_value()) {
         co_await active_->rollback();
         active_.reset();
         values_.reset();
      }
      co_return value;
   }

 private:
   store owner_;
   key_range range_;
   stream_options options_;
   std::optional<transaction> active_;
   std::optional<index_stream<value_type>> values_;
};

template <object_model Object, typename Tag>
class store_range_query {
 public:
   using value_type = typename Object::value_type;

   store_range_query() = default;

   store_range_query(store owner, key_range range) : owner_{std::move(owner)}, range_{std::move(range)} {}

   boost::asio::awaitable<object_page<value_type>> page(page_request request = {}) {
      auto tx = co_await owner_.begin_transaction();
      auto failure = std::exception_ptr{};
      try {
         auto result = co_await range_query<Object, Tag>{tx, range_}.page(std::move(request));
         co_await tx.rollback();
         co_return result;
      } catch (...) {
         failure = std::current_exception();
      }
      co_await tx.rollback();
      std::rethrow_exception(failure);
   }

   [[nodiscard]] store_index_stream<Object, Tag> stream(stream_options options = {}) {
      return store_index_stream<Object, Tag>{owner_, range_, options};
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
   store owner_;
   key_range range_;
};

template <object_model Object, typename Tag>
class store_index_view {
 public:
   using value_type = typename Object::value_type;

   explicit store_index_view(store owner) : owner_{std::move(owner)} {}

   template <typename Key>
   boost::asio::awaitable<std::optional<value_type>> find(const Key& key) {
      auto result = co_await equal_range(std::tuple{key}).page(page_request{.limit = 1});
      if (result.items.empty()) {
         co_return std::nullopt;
      }
      co_return result.items.front();
   }

   template <typename... PrefixValues>
   [[nodiscard]] store_range_query<Object, Tag> equal_range(const std::tuple<PrefixValues...>& prefix) const {
      return store_range_query<Object, Tag>{owner_, layout<Object>::template index_prefix<Tag>(prefix)};
   }

   template <typename... PrefixValues>
   [[nodiscard]] store_range_query<Object, Tag> equal_range(const PrefixValues&... values) const {
      return equal_range(std::make_tuple(values...));
   }

   template <typename... PrefixValues>
   [[nodiscard]] store_range_query<Object, Tag> lower_bound(const std::tuple<PrefixValues...>& prefix) const {
      auto range = layout<Object>::template index_range<Tag>();
      range.begin = layout<Object>::template index_prefix<Tag>(prefix).begin;
      return store_range_query<Object, Tag>{owner_, std::move(range)};
   }

   template <typename... PrefixValues>
   [[nodiscard]] store_range_query<Object, Tag> upper_bound(const std::tuple<PrefixValues...>& prefix) const {
      auto range = layout<Object>::template index_range<Tag>();
      auto exact = layout<Object>::template index_prefix<Tag>(prefix);
      range.begin = exact.has_end ? std::move(exact.end) : std::move(exact.begin);
      return store_range_query<Object, Tag>{owner_, std::move(range)};
   }

 private:
   store owner_;
};

template <object_model Object, typename Tag>
[[nodiscard]] store_index_view<Object, Tag> store::index() const {
   impl_->template ensure_registered<Object>();
   return store_index_view<Object, Tag>{*this};
}

} // namespace forge::objectdb
