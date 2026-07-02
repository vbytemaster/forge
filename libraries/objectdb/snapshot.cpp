module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <typeindex>
#include <utility>

module forge.objectdb.snapshot;

import forge.objectdb.exceptions;

#include "details/snapshot_impl.hxx"

namespace forge::objectdb {

snapshot::impl::impl(std::unique_ptr<session> active_value, snapshot::ensure_registered_fn ensure) noexcept
    : active{std::move(active_value)}, ensure_registered{std::move(ensure)} {}

snapshot::snapshot(std::unique_ptr<session> active, ensure_registered_fn ensure)
    : impl_{std::make_shared<impl>(std::move(active), std::move(ensure))} {}

session& snapshot::active_session() const {
   if (!impl_ || !impl_->active || impl_->closed) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb snapshot is closed");
   }
   return *impl_->active;
}

void snapshot::ensure_registered_type(forge::ids::object_id type, std::type_index model) const {
   if (!impl_ || !impl_->ensure_registered) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "objectdb snapshot is closed");
   }
   impl_->ensure_registered(type, model);
}

} // namespace forge::objectdb
