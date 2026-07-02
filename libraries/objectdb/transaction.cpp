module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/system_executor.hpp>

#include <memory>
#include <typeindex>
#include <utility>

module forge.objectdb.transaction;

import forge.objectdb.exceptions;

#include "details/transaction_impl.hxx"

namespace forge::objectdb {

namespace {

boost::asio::awaitable<void> rollback_dropped_transaction(std::unique_ptr<session> active,
                                                          transaction::release_fn release) {
   try {
      co_await active->rollback();
   } catch (...) {
   }

   active.reset();
   if (release) {
      release();
   }
   co_return;
}

} // namespace

transaction::impl::impl(std::unique_ptr<session> active_value,
                        transaction::ensure_registered_fn ensure,
                        std::vector<std::shared_ptr<interceptor>> interceptors_value,
                        std::vector<std::shared_ptr<observer>> observers_value,
                        transaction::release_fn release,
                        boost::asio::any_io_executor executor) noexcept
    : active{std::move(active_value)},
      ensure_registered{std::move(ensure)},
      interceptors{std::move(interceptors_value)},
      observers{std::move(observers_value)},
      release_writer{std::move(release)},
      cleanup_executor{std::move(executor)} {}

transaction::impl::~impl() {
   rollback_on_drop();
}

void transaction::impl::release() noexcept {
   if (release_writer) {
      release_writer();
      release_writer = {};
   }
}

void transaction::impl::rollback_on_drop() noexcept {
   if (!active || closed || committed) {
      active.reset();
      release();
      return;
   }

   closed = true;
   auto dropped = std::move(active);
   auto release = std::move(release_writer);
   release_writer = {};

   try {
      boost::asio::co_spawn(*cleanup_executor,
                            rollback_dropped_transaction(std::move(dropped), std::move(release)),
                            boost::asio::detached);
   } catch (...) {
      dropped.reset();
      if (release) {
         release();
      }
   }
}

transaction::transaction(std::unique_ptr<session> active,
                         ensure_registered_fn ensure,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers,
                         release_fn release)
    : transaction(std::move(active),
                  std::move(ensure),
                  std::move(interceptors),
                  std::move(observers),
                  std::move(release),
                  boost::asio::system_executor{}) {}

transaction::transaction(std::unique_ptr<session> active,
                         ensure_registered_fn ensure,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers,
                         release_fn release,
                         boost::asio::any_io_executor cleanup_executor)
    : impl_{std::make_shared<impl>(
         std::move(active),
         std::move(ensure),
         std::move(interceptors),
         std::move(observers),
         std::move(release),
         std::move(cleanup_executor))} {}

session& transaction::active_session() const {
   if (!impl_ || !impl_->active || impl_->closed) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb transaction is closed");
   }
   return *impl_->active;
}

change_set& transaction::changes() const {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb transaction is closed");
   }
   return impl_->changes;
}

void transaction::ensure_registered_type(forge::ids::object_id type, std::type_index model) const {
   if (!impl_ || !impl_->ensure_registered) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb transaction is closed");
   }
   impl_->ensure_registered(type, model);
}

boost::asio::awaitable<void> transaction::before_mutation(const object_mutation& mutation) const {
   for (const auto& hook : impl_->interceptors) {
      co_await hook->before_mutation(mutation);
   }
   co_return;
}

boost::asio::awaitable<void> transaction::commit() {
   auto& current = active_session();
   co_await current.commit();
   impl_->committed = true;
   impl_->closed = true;
   impl_->active.reset();
   auto changes = std::move(impl_->changes);
   impl_->release();

   if (!changes.empty()) {
      for (const auto& hook : impl_->observers) {
         co_await hook->after_commit(changes);
      }
   }
   co_return;
}

boost::asio::awaitable<void> transaction::rollback() {
   if (!impl_ || !impl_->active || impl_->closed) {
      co_return;
   }
   co_await impl_->active->rollback();
   impl_->active.reset();
   impl_->closed = true;
   impl_->changes.mutations.clear();
   impl_->release();
   co_return;
}

} // namespace forge::objectdb
