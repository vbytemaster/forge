module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <typeindex>
#include <typeinfo>
#include <utility>

export module forge.objectdb.store;

import forge.ids.object_id;
import forge.objectdb.cursor;
import forge.objectdb.exceptions;
import forge.objectdb.hooks;
import forge.objectdb.index;
import forge.objectdb.object;
import forge.objectdb.record;
import forge.objectdb.session;
import forge.objectdb.snapshot;
import forge.objectdb.transaction;

export namespace forge::objectdb {

enum class write_policy : std::uint8_t {
   single_writer,
   backend,
};

class store {
 public:
   struct options {
      write_policy writes = write_policy::single_writer;
   };

   template <session_model Session>
   explicit store(session_factory<Session> factory)
       : store(std::move(factory), options{}) {}

   template <session_model Session>
   store(session_factory<Session> factory, options value)
       : store(factory, factory, value) {}

   template <session_model WriteSession, session_model ReadSession>
   store(session_factory<WriteSession> write, session_factory<ReadSession> read)
       : store(std::move(write), std::move(read), options{}) {}

   template <session_model WriteSession, session_model ReadSession>
   store(session_factory<WriteSession> write, session_factory<ReadSession> read, options value)
       : store(
            [write = std::move(write)]() mutable -> boost::asio::awaitable<std::unique_ptr<session>> {
               co_return co_await write.begin();
            },
            [read = std::move(read)]() mutable -> boost::asio::awaitable<std::unique_ptr<session>> {
               co_return co_await read.begin();
            },
            value) {}

   template <object_model Object>
   void register_object();

   void add_interceptor(std::shared_ptr<interceptor> value);
   void add_observer(std::shared_ptr<observer> value);

   boost::asio::awaitable<transaction> begin_transaction();
   boost::asio::awaitable<snapshot> begin_read();

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> get(Id id);

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> find(Id id);

   template <object_model Object>
   boost::asio::awaitable<typename Object::value_type> get(forge::ids::object_id id);

   template <object_model Object>
   boost::asio::awaitable<std::optional<typename Object::value_type>> find(forge::ids::object_id id);

   template <object_value Value>
   boost::asio::awaitable<void> insert(Value value);

   template <object_value Value>
   boost::asio::awaitable<void> replace(Value value);

   template <forge::ids::typed_id_like Id, typename Fn>
   boost::asio::awaitable<void> modify(Id id, Fn&& fn);

   template <forge::ids::typed_id_like Id>
   boost::asio::awaitable<void> erase(Id id);

   template <object_model Object>
   boost::asio::awaitable<void> erase(forge::ids::object_id id);

   template <object_model Object, typename Tag>
   [[nodiscard]] index_view<Object, Tag> index() const;

 private:
   friend class transaction;
   friend class snapshot;

   using begin_fn = std::function<boost::asio::awaitable<std::unique_ptr<session>>()>;

   store(begin_fn write, begin_fn read, options value);

   void register_object_type(forge::ids::object_id type, std::type_index model);
   void ensure_registered_type(forge::ids::object_id type, std::type_index model) const;

   struct impl;
   std::shared_ptr<impl> impl_;
};

template <object_model Object>
void store::register_object() {
   register_object_type(object_id_of<Object>::value, std::type_index{typeid(Object)});
}

template <forge::ids::typed_id_like Id>
boost::asio::awaitable<typename object_index_for_id_t<Id>::value_type> store::get(Id id) {
   const auto value = co_await find(id);
   if (!value.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb object was not found");
   }
   co_return *value;
}

template <forge::ids::typed_id_like Id>
boost::asio::awaitable<std::optional<typename object_index_for_id_t<Id>::value_type>> store::find(Id id) {
   auto read = co_await begin_read();
   co_return co_await read.find(id);
}

template <object_model Object>
boost::asio::awaitable<typename Object::value_type> store::get(forge::ids::object_id id) {
   const auto value = co_await find<Object>(id);
   if (!value.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "objectdb object was not found");
   }
   co_return *value;
}

template <object_model Object>
boost::asio::awaitable<std::optional<typename Object::value_type>> store::find(forge::ids::object_id id) {
   auto read = co_await begin_read();
   co_return co_await read.template find<Object>(id);
}

template <object_value Value>
boost::asio::awaitable<void> store::insert(Value value) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   try {
      co_await active.insert(std::move(value));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
}

template <object_value Value>
boost::asio::awaitable<void> store::replace(Value value) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   try {
      co_await active.replace(std::move(value));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
}

template <forge::ids::typed_id_like Id, typename Fn>
boost::asio::awaitable<void> store::modify(Id id, Fn&& fn) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   try {
      co_await active.modify(id, std::forward<Fn>(fn));
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
}

template <forge::ids::typed_id_like Id>
boost::asio::awaitable<void> store::erase(Id id) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   try {
      co_await active.erase(id);
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
}

template <object_model Object>
boost::asio::awaitable<void> store::erase(forge::ids::object_id id) {
   auto active = co_await begin_transaction();
   auto error = std::exception_ptr{};
   try {
      co_await active.template erase<Object>(id);
      co_await active.commit();
   } catch (...) {
      error = std::current_exception();
   }
   if (error) {
      co_await active.rollback();
      std::rethrow_exception(error);
   }
}

template <object_model Object, typename Tag>
[[nodiscard]] index_view<Object, Tag> store::index() const {
   ensure_registered_type(object_id_of<Object>::value, std::type_index{typeid(Object)});

   return index_view<Object, Tag>{
      [owner = *this](record_range range, page_request request) mutable
         -> boost::asio::awaitable<object_page<typename Object::value_type>> {
         auto read = co_await owner.begin_read();
         auto view = read.template index<Object, Tag>();
         co_return co_await view.page(std::move(range), std::move(request));
      },
      [owner = *this]() mutable -> index_page_query<typename Object::value_type> {
         auto active = std::make_shared<std::optional<snapshot>>();
         return [owner, active](record_range range, page_request request) mutable
                   -> boost::asio::awaitable<object_page<typename Object::value_type>> {
            if (!active->has_value()) {
               active->emplace(co_await owner.begin_read());
            }
            auto view = active->value().template index<Object, Tag>();
            co_return co_await view.page(std::move(range), std::move(request));
         };
      }};
}

} // namespace forge::objectdb
