module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

module forge.objectdb.store;

import forge.objectdb.exceptions;

#include "details/store_impl.hxx"

namespace forge::objectdb {

store::impl::impl(begin_fn write, begin_fn read, store::options value)
    : begin_write{std::move(write)},
      begin_read{std::move(read)},
      settings{value},
      write_gate{std::make_shared<detail::write_gate>()} {}

boost::asio::awaitable<std::unique_ptr<session>> store::impl::open_write_session() const {
   auto active = co_await open_session(begin_write, "objectdb store has no write session factory");
   const auto caps = active->capabilities();
   if (!caps.writes) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "objectdb session does not support writes");
   }
   co_return active;
}

boost::asio::awaitable<std::unique_ptr<session>> store::impl::open_read_session() const {
   auto active = co_await open_session(begin_read, "objectdb store has no read session factory");
   const auto caps = active->capabilities();
   if (!caps.snapshot_reads) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "objectdb session does not support snapshot reads");
   }
   co_return active;
}

void store::impl::register_object_type(forge::ids::object_id type, std::type_index model) {
   if (registered.contains(type)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "objectdb object type is already registered");
   }
   registered.emplace(type, model);
}

void store::impl::ensure_registered_type(forge::ids::object_id type, std::type_index model) const {
   const auto found = registered.find(type);
   if (found == registered.end() || found->second != model) {
      FORGE_THROW_EXCEPTION(exceptions::unregistered_object, "objectdb object type is not registered");
   }
}

boost::asio::awaitable<std::unique_ptr<session>> store::impl::open_session(const begin_fn& begin,
                                                                           std::string_view message) const {
   if (!begin) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, std::string{message});
   }
   auto active = co_await begin();
   if (!active) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "objectdb session factory returned null session");
   }
   const auto caps = active->capabilities();
   if (!caps.snapshot_reads && !caps.writes) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "objectdb session supports neither snapshots nor writes");
   }
   co_return active;
}

} // namespace forge::objectdb
