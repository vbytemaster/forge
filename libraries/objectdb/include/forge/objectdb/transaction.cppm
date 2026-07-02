module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.objectdb.transaction;

import forge.ids.types;
import forge.objectdb.cursor;
import forge.objectdb.exceptions;
import forge.objectdb.hooks;
import forge.objectdb.index;
import forge.objectdb.object;
import forge.objectdb.record;
import forge.objectdb.session;
import forge.raw.raw;

export namespace forge::objectdb {

class transaction {
 public:
   using ensure_registered_fn = std::function<void(forge::ids::object_id, std::type_index)>;
   using release_fn = std::function<void()>;

   transaction() = default;
   transaction(std::unique_ptr<session> active,
               ensure_registered_fn ensure,
               std::vector<std::shared_ptr<interceptor>> interceptors,
               std::vector<std::shared_ptr<observer>> observers,
               release_fn release);

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
   [[nodiscard]] index_view<Object, Tag> index() const;

   boost::asio::awaitable<void> commit();
   boost::asio::awaitable<void> rollback();

 private:
   class access;

   [[nodiscard]] session& active_session() const;
   [[nodiscard]] change_set& changes() const;

   void ensure_registered_type(forge::ids::object_id type, std::type_index model) const;
   boost::asio::awaitable<void> before_mutation(const object_mutation& mutation) const;

   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::objectdb

namespace forge::objectdb {

class transaction::access {
 public:
   explicit access(const transaction& owner) : owner_{owner} {}

   [[nodiscard]] session& active_session() const {
      return owner_.active_session();
   }

   [[nodiscard]] change_set& changes() const {
      return owner_.changes();
   }

   template <object_model Object>
   void ensure_registered() const {
      owner_.ensure_registered_type(object_id_of<Object>::value, std::type_index{typeid(Object)});
   }

   boost::asio::awaitable<void> before_mutation(const object_mutation& mutation) const {
      return owner_.before_mutation(mutation);
   }

 private:
   const transaction& owner_;
};

} // namespace forge::objectdb

namespace forge::objectdb::detail {

template <object_model Object, typename Access>
boost::asio::awaitable<std::optional<typename Object::value_type>> read_transaction_object(Access tx,
                                                                                           forge::ids::object_id id) {
   tx.template ensure_registered<Object>();
   const auto typed = typed_id_from<Object>(id);
   const auto key = object_record_key<Object>(typed);
   const auto bytes = co_await tx.active_session().get(key);
   if (!bytes.has_value()) {
      co_return std::nullopt;
   }
   co_return unpack_value<typename Object::value_type>(*bytes);
}

template <object_model Object, typename Access>
boost::asio::awaitable<object_page<typename Object::value_type>> page_transaction_objects(Access tx,
                                                                                          record_range range,
                                                                                          page_request request) {
   tx.template ensure_registered<Object>();
   validate_page_request(request);

   auto records = co_await tx.active_session().scan_page(std::move(range), std::move(request));
   auto out = object_page<typename Object::value_type>{};
   out.next = std::move(records.next).transform([](record_key key) { return cursor{.boundary = std::move(key)}; });

   for (const auto& entry : records.entries) {
      const auto id = unpack_value<id_type_of<Object>>(entry.value);
      auto value = co_await read_transaction_object<Object>(tx, id.as_object_id());
      if (!value.has_value()) {
         FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb index points to a missing object");
      }
      out.items.push_back(std::move(*value));
   }

   co_return out;
}

template <object_model Object, typename Access, std::size_t Index = 0>
boost::asio::awaitable<void> verify_unique_indexes(Access tx, const typename Object::value_type& value) {
   using indexes = typename Object::indexes_type::tuple_type;
   if constexpr (Index < std::tuple_size_v<indexes>) {
      using index = std::tuple_element_t<Index, indexes>;
      if constexpr (index::kind == index_kind::secondary_unique) {
         const auto key = index_entry_key<Object, typename index::tag_type>(value);
         const auto existing = co_await tx.active_session().get(key);
         if (existing.has_value()) {
            const auto existing_id = unpack_value<id_type_of<Object>>(*existing);
            if (existing_id != value.id) {
               FORGE_THROW_EXCEPTION(exceptions::duplicate_object, "objectdb unique index value already exists");
            }
         }
      }
      co_await verify_unique_indexes<Object, Access, Index + 1>(tx, value);
   }
   co_return;
}

template <object_model Object, typename Access, std::size_t Index = 0>
boost::asio::awaitable<void> write_secondary_indexes(Access tx, const typename Object::value_type& value) {
   using indexes = typename Object::indexes_type::tuple_type;
   if constexpr (Index < std::tuple_size_v<indexes>) {
      using index = std::tuple_element_t<Index, indexes>;
      if constexpr (secondary_index<index>) {
         const auto key = index_entry_key<Object, typename index::tag_type>(value);
         co_await tx.active_session().put(key, pack_value(value.id));
      }
      co_await write_secondary_indexes<Object, Access, Index + 1>(tx, value);
   }
   co_return;
}

template <object_model Object, typename Access, std::size_t Index = 0>
boost::asio::awaitable<void> remove_secondary_indexes(Access tx, const typename Object::value_type& value) {
   using indexes = typename Object::indexes_type::tuple_type;
   if constexpr (Index < std::tuple_size_v<indexes>) {
      using index = std::tuple_element_t<Index, indexes>;
      if constexpr (secondary_index<index>) {
         const auto key = index_entry_key<Object, typename index::tag_type>(value);
         co_await tx.active_session().erase(key);
      }
      co_await remove_secondary_indexes<Object, Access, Index + 1>(tx, value);
   }
   co_return;
}

template <typename Access, object_value Value>
boost::asio::awaitable<void> insert_object(Access tx, Value value) {
   using object_model_type = object_index_for_id_t<typename Value::id_type>;
   tx.template ensure_registered<object_model_type>();

   const auto object_key = object_record_key<object_model_type>(value.id);
   if ((co_await tx.active_session().get(object_key)).has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::duplicate_object, "objectdb object id already exists");
   }

   auto after = pack_value(value);
   auto mutation = object_mutation{
      .kind = mutation_kind::insert,
      .id = value.id.as_object_id(),
      .after = after,
   };
   co_await tx.before_mutation(mutation);
   co_await verify_unique_indexes<object_model_type>(tx, value);
   co_await tx.active_session().put(object_key, std::move(after));
   co_await write_secondary_indexes<object_model_type>(tx, value);
   tx.changes().mutations.push_back(std::move(mutation));
   co_return;
}

template <typename Access, object_value Value>
boost::asio::awaitable<void> replace_object(Access tx, Value value, mutation_kind kind) {
   using object_model_type = object_index_for_id_t<typename Value::id_type>;
   tx.template ensure_registered<object_model_type>();

   const auto existing = co_await read_transaction_object<object_model_type>(tx, value.id.as_object_id());
   if (!existing.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb object was not found");
   }

   auto before = pack_value(*existing);
   auto after = pack_value(value);
   auto mutation = object_mutation{
      .kind = kind,
      .id = value.id.as_object_id(),
      .before = before,
      .after = after,
   };
   co_await tx.before_mutation(mutation);
   co_await verify_unique_indexes<object_model_type>(tx, value);
   co_await remove_secondary_indexes<object_model_type>(tx, *existing);
   co_await tx.active_session().put(object_record_key<object_model_type>(value.id), std::move(after));
   co_await write_secondary_indexes<object_model_type>(tx, value);
   tx.changes().mutations.push_back(std::move(mutation));
   co_return;
}

template <object_model Object, typename Access>
boost::asio::awaitable<void> erase_object(Access tx, forge::ids::object_id id) {
   tx.template ensure_registered<Object>();
   const auto typed = typed_id_from<Object>(id);
   const auto existing = co_await read_transaction_object<Object>(tx, typed.as_object_id());
   if (!existing.has_value()) {
      co_return;
   }

   auto before = pack_value(*existing);
   auto mutation = object_mutation{
      .kind = mutation_kind::erase,
      .id = typed.as_object_id(),
      .before = before,
   };
   co_await tx.before_mutation(mutation);
   co_await remove_secondary_indexes<Object>(tx, *existing);
   co_await tx.active_session().erase(object_record_key<Object>(typed));
   tx.changes().mutations.push_back(std::move(mutation));
   co_return;
}

} // namespace forge::objectdb::detail

export namespace forge::objectdb {

template <typed_object_id Id>
boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> transaction::get(Id id) {
   co_return co_await get<object_index_for_id_t<Id>>(id.as_object_id());
}

template <typed_object_id Id>
boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> transaction::find(Id id) {
   co_return co_await find<object_index_for_id_t<Id>>(id.as_object_id());
}

template <object_model Object>
boost::asio::awaitable<typename Object::value_type> transaction::get(forge::ids::object_id id) {
   const auto value = co_await find<Object>(id);
   if (!value.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb object was not found");
   }
   co_return *value;
}

template <object_model Object>
boost::asio::awaitable<std::optional<typename Object::value_type>> transaction::find(forge::ids::object_id id) {
   co_return co_await detail::read_transaction_object<Object>(access{*this}, id);
}

template <object_value Value>
boost::asio::awaitable<void> transaction::insert(Value value) {
   co_await detail::insert_object(access{*this}, std::move(value));
   co_return;
}

template <object_value Value>
boost::asio::awaitable<void> transaction::replace(Value value) {
   co_await detail::replace_object(access{*this}, std::move(value), mutation_kind::replace);
   co_return;
}

template <typed_object_id Id, typename Fn>
boost::asio::awaitable<void> transaction::modify(Id id, Fn&& fn) {
   using object_model_type = object_index_for_id_t<Id>;
   auto next = co_await get(id);
   using result_type = std::invoke_result_t<Fn&, typename object_model_type::value_type&>;
   static_assert(std::is_void_v<result_type>, "objectdb modify mutator must return void");
   std::invoke(fn, next);
   co_await detail::replace_object(access{*this}, std::move(next), mutation_kind::modify);
   co_return;
}

template <typed_object_id Id>
boost::asio::awaitable<void> transaction::erase(Id id) {
   co_await erase<object_index_for_id_t<Id>>(id.as_object_id());
   co_return;
}

template <object_model Object>
boost::asio::awaitable<void> transaction::erase(forge::ids::object_id id) {
   co_await detail::erase_object<Object>(access{*this}, id);
   co_return;
}

template <object_model Object, typename Tag>
[[nodiscard]] index_view<Object, Tag> transaction::index() const {
   access{*this}.template ensure_registered<Object>();
   return index_view<Object, Tag>{
      [owner = *this](record_range range, page_request request) mutable
         -> boost::asio::awaitable<object_page<typename Object::value_type>> {
         co_return co_await detail::page_transaction_objects<Object>(
            access{owner},
            std::move(range),
            std::move(request));
      }};
}

} // namespace forge::objectdb
