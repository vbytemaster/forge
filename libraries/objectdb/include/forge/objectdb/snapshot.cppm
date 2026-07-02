module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

export module forge.objectdb.snapshot;

import forge.ids.types;
import forge.objectdb.cursor;
import forge.objectdb.exceptions;
import forge.objectdb.index;
import forge.objectdb.object;
import forge.objectdb.record;
import forge.objectdb.session;
import forge.raw.raw;

export namespace forge::objectdb {

class snapshot {
 public:
   using ensure_registered_fn = std::function<void(forge::ids::object_id, std::type_index)>;

   snapshot() = default;
   snapshot(std::unique_ptr<session> active, ensure_registered_fn ensure);

   template <typed_object_id Id>
   boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> get(Id id);

   template <typed_object_id Id>
   boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> find(Id id);

   template <object_model Object>
   boost::asio::awaitable<typename Object::value_type> get(forge::ids::object_id id);

   template <object_model Object>
   boost::asio::awaitable<std::optional<typename Object::value_type>> find(forge::ids::object_id id);

   template <object_model Object, typename Tag>
   [[nodiscard]] index_view<Object, Tag> index() const;

 private:
   class access;

   [[nodiscard]] session& active_session() const;

   void ensure_registered_type(forge::ids::object_id type, std::type_index model) const;

   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::objectdb

namespace forge::objectdb {

class snapshot::access {
 public:
   explicit access(const snapshot& owner) : owner_{owner} {}

   [[nodiscard]] session& active_session() const {
      return owner_.active_session();
   }

   template <object_model Object>
   void ensure_registered() const {
      owner_.ensure_registered_type(object_id_of<Object>::value, std::type_index{typeid(Object)});
   }

 private:
   const snapshot& owner_;
};

} // namespace forge::objectdb

namespace forge::objectdb::detail {

template <object_model Object, typename Access>
boost::asio::awaitable<std::optional<typename Object::value_type>> read_snapshot_object(Access view,
                                                                                        forge::ids::object_id id) {
   view.template ensure_registered<Object>();
   const auto typed = typed_id_from<Object>(id);
   const auto key = object_record_key<Object>(typed);
   const auto bytes = co_await view.active_session().get(key);
   if (!bytes.has_value()) {
      co_return std::nullopt;
   }
   co_return unpack_value<typename Object::value_type>(*bytes);
}

template <object_model Object, typename Access>
boost::asio::awaitable<object_page<typename Object::value_type>> page_snapshot_objects(Access view,
                                                                                       record_range range,
                                                                                       page_request request) {
   view.template ensure_registered<Object>();
   validate_page_request(request);

   auto records = co_await view.active_session().scan_page(std::move(range), std::move(request));
   auto out = object_page<typename Object::value_type>{};
   out.next = std::move(records.next).transform([](record_key key) { return cursor{.boundary = std::move(key)}; });

   for (const auto& entry : records.entries) {
      const auto id = unpack_value<id_type_of<Object>>(entry.value);
      auto value = co_await read_snapshot_object<Object>(view, id.as_object_id());
      if (!value.has_value()) {
         FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb index points to a missing object");
      }
      out.items.push_back(std::move(*value));
   }

   co_return out;
}

} // namespace forge::objectdb::detail

export namespace forge::objectdb {

template <typed_object_id Id>
boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> snapshot::get(Id id) {
   co_return co_await get<object_index_for_id_t<Id>>(id.as_object_id());
}

template <typed_object_id Id>
boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> snapshot::find(Id id) {
   co_return co_await find<object_index_for_id_t<Id>>(id.as_object_id());
}

template <object_model Object>
boost::asio::awaitable<typename Object::value_type> snapshot::get(forge::ids::object_id id) {
   const auto value = co_await find<Object>(id);
   if (!value.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb object was not found");
   }
   co_return *value;
}

template <object_model Object>
boost::asio::awaitable<std::optional<typename Object::value_type>> snapshot::find(forge::ids::object_id id) {
   co_return co_await detail::read_snapshot_object<Object>(access{*this}, id);
}

template <object_model Object, typename Tag>
[[nodiscard]] index_view<Object, Tag> snapshot::index() const {
   access{*this}.template ensure_registered<Object>();
   return index_view<Object, Tag>{
      [owner = *this](record_range range, page_request request) mutable
         -> boost::asio::awaitable<object_page<typename Object::value_type>> {
         co_return co_await detail::page_snapshot_objects<Object>(access{owner}, std::move(range), std::move(request));
      },
      [owner = *this]() mutable -> index_page_query<typename Object::value_type> {
         return [owner](record_range range, page_request request) mutable
                   -> boost::asio::awaitable<object_page<typename Object::value_type>> {
            co_return co_await detail::page_snapshot_objects<Object>(
               access{owner},
               std::move(range),
               std::move(request));
         };
      }};
}

} // namespace forge::objectdb
