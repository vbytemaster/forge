module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <utility>

module forge.db.authenticated.store;

import forge.db.authenticated.exceptions;
import forge.db.authenticated.tree_engine;

#include "details/store_impl.hxx"

namespace forge::db::authenticated {

store::impl::impl(std::shared_ptr<forge::db::core::driver> driver_value, config settings_value)
    : driver{std::move(driver_value)}, settings{std::move(settings_value)}, namespace_hash{} {
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_store, "authenticated store driver is null");
   }
   if (settings.domain.empty() || settings.domain.size() > max_base_domain_bytes || settings.family.name.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_store, "authenticated store domain and family must not be empty");
   }
   if (!limits_are_valid(settings.bounds)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_store, "authenticated store proof limits are invalid");
   }
   namespace_hash = detail::namespace_id(settings.domain);
}

} // namespace forge::db::authenticated
