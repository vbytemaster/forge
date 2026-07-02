#pragma once

#include "write_gate.hxx"

namespace forge::objectdb {

struct store::impl {
   using begin_fn = std::function<boost::asio::awaitable<std::unique_ptr<session>>()>;

   impl(begin_fn write, begin_fn read, store::options value);

   boost::asio::awaitable<std::unique_ptr<session>> open_write_session() const;
   boost::asio::awaitable<std::unique_ptr<session>> open_read_session() const;

   void register_object_type(forge::ids::object_id type, std::type_index model);
   void ensure_registered_type(forge::ids::object_id type, std::type_index model) const;

   begin_fn begin_write;
   begin_fn begin_read;
   store::options settings;
   std::shared_ptr<detail::write_gate> write_gate;
   std::map<forge::ids::object_id, std::type_index> registered;
   std::vector<std::shared_ptr<interceptor>> interceptors;
   std::vector<std::shared_ptr<observer>> observers;

 private:
   boost::asio::awaitable<std::unique_ptr<session>> open_session(const begin_fn& begin,
                                                                 std::string_view message) const;
};

} // namespace forge::objectdb
