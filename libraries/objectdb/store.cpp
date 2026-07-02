module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

module forge.objectdb.store;

import forge.objectdb.exceptions;

#include "details/store_impl.hxx"

namespace forge::objectdb {

store::store(begin_fn write, begin_fn read, options value)
    : impl_{std::make_shared<impl>(std::move(write), std::move(read), value)} {}

void store::add_interceptor(std::shared_ptr<interceptor> value) {
   if (value) {
      impl_->interceptors.push_back(std::move(value));
   }
}

void store::add_observer(std::shared_ptr<observer> value) {
   if (value) {
      impl_->observers.push_back(std::move(value));
   }
}

boost::asio::awaitable<transaction> store::begin_transaction() {
   auto ticket = std::optional<detail::write_gate::ticket>{};
   if (impl_->settings.writes == write_policy::single_writer) {
      ticket.emplace(co_await impl_->write_gate->acquire());
   }
   auto active = co_await impl_->open_write_session();
   auto release = transaction::release_fn{};
   if (ticket.has_value()) {
      auto owned_ticket =
         std::make_shared<std::optional<detail::write_gate::ticket>>(std::move(ticket));
      release = [owned_ticket]() mutable { owned_ticket->reset(); };
   }
   co_return transaction{
      std::move(active),
      [impl = impl_](forge::ids::object_id type, std::type_index model) {
         impl->ensure_registered_type(type, model);
      },
      impl_->interceptors,
      impl_->observers,
      std::move(release)};
}

boost::asio::awaitable<snapshot> store::begin_read() {
   auto active = co_await impl_->open_read_session();
   co_return snapshot{
      std::move(active),
      [impl = impl_](forge::ids::object_id type, std::type_index model) {
         impl->ensure_registered_type(type, model);
      }};
}

void store::register_object_type(forge::ids::object_id type, std::type_index model) {
   impl_->register_object_type(type, model);
}

void store::ensure_registered_type(forge::ids::object_id type, std::type_index model) const {
   impl_->ensure_registered_type(type, model);
}

} // namespace forge::objectdb
