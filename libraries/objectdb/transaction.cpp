module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <typeindex>
#include <utility>

module forge.objectdb.transaction;

import forge.objectdb.exceptions;

#include "details/transaction_impl.hxx"

namespace forge::objectdb {

transaction::impl::impl(std::unique_ptr<session> active_value,
                        transaction::ensure_registered_fn ensure,
                        std::vector<std::shared_ptr<interceptor>> interceptors_value,
                        std::vector<std::shared_ptr<observer>> observers_value,
                        transaction::release_fn release) noexcept
    : active{std::move(active_value)},
      ensure_registered{std::move(ensure)},
      interceptors{std::move(interceptors_value)},
      observers{std::move(observers_value)},
      release_writer{std::move(release)} {}

transaction::impl::~impl() {
   active.reset();
   release();
}

void transaction::impl::release() noexcept {
   if (release_writer) {
      release_writer();
      release_writer = {};
   }
}

transaction::transaction(std::unique_ptr<session> active,
                         ensure_registered_fn ensure,
                         std::vector<std::shared_ptr<interceptor>> interceptors,
                         std::vector<std::shared_ptr<observer>> observers,
                         release_fn release)
    : impl_{std::make_shared<impl>(
         std::move(active), std::move(ensure), std::move(interceptors), std::move(observers), std::move(release))} {}

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
