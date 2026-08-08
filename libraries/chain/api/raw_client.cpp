module;

#include <forge/exceptions/macros.hpp>

#include <utility>

module forge.chain.api.raw_client;

import forge.chain.api.exceptions;

namespace forge::chain::api {

namespace {

template <typename Interface> Interface& require(const forge::api::core::handle<Interface>& value, const char* name) {
   if (!value) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API service is unavailable",
                            forge::exceptions::ctx("service", name));
   }
   return *value.shared();
}

} // namespace

raw_client::raw_client(service_handles services) : services_{std::move(services)} {}

const service_handles& raw_client::services() const noexcept {
   return services_;
}

forge::chain::api::info& raw_client::info() const {
   return require(services_.information, "info");
}

forge::chain::api::block& raw_client::blocks() const {
   return require(services_.blocks, "block");
}

forge::chain::api::state& raw_client::state() const {
   return require(services_.state_queries, "state");
}

forge::chain::api::transaction& raw_client::transactions() const {
   return require(services_.transactions, "transaction");
}

} // namespace forge::chain::api
