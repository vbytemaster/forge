module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>
#include <forge/api/http/macros.hpp>

#include <type_traits>
#include <vector>

export module forge.chain.api.transaction;

import forge.api.core.binding;
import forge.api.core.connection;
import forge.api.core.descriptor;
import forge.api.core.dispatcher;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.registry;
import forge.api.core.types;
import forge.api.http.binding;
import forge.api.http.client_request;
import forge.api.http.mapping;
import forge.api.http.openapi;
import forge.api.http.proxy;
import forge.chain.api.json_schema;
import forge.crypto.asymmetric;
import forge.crypto.digest.sha256;
import forge.net.http.types;
import forge.variant.variant_dynamic_bitset;

export import forge.chain.api.exceptions;
export import forge.chain.protocol.transaction_query;

export namespace forge::chain::api {

class transaction : public forge::api::core::contract<transaction, forge::api::core::surface::local |
                                                                       forge::api::core::surface::remote> {
 public:
   virtual ~transaction() = default;

   virtual boost::asio::awaitable<protocol::transaction_status_response>
   get_status(protocol::transaction_status_request value) = 0;
   virtual boost::asio::awaitable<protocol::transaction_status_response>
   await_transaction(protocol::transaction_await_request value) = 0;
   virtual boost::asio::awaitable<std::vector<protocol::public_key>>
   get_required_keys(protocol::transaction_required_keys_request value) = 0;
   virtual boost::asio::awaitable<protocol::transaction_read_only_response>
   compute_transaction(protocol::transaction_read_only_request value) = 0;
   virtual boost::asio::awaitable<protocol::transaction_read_only_response>
   send_read_only_transaction(protocol::transaction_read_only_request value) = 0;
};

} // namespace forge::chain::api

export namespace forge::api::core {

template <> struct method_descriptor_customization<::forge::chain::api::transaction> {
   template <auto Method, bool EnableRaw>
   static void apply(method_builder<::forge::chain::api::transaction, EnableRaw>& method) {
      using response_type = method_response_t<Method>;
      if constexpr (std::is_base_of_v<::forge::chain::protocol::audited_response, response_type>) {
         ::forge::chain::api::exceptions::descriptor::declare_historical_query<Method>(method);
         if constexpr (std::is_same_v<method_request_t<Method>, ::forge::chain::protocol::transaction_await_request>) {
            ::forge::chain::api::exceptions::descriptor::declare_deadline(method);
         }
      } else {
         ::forge::chain::api::exceptions::descriptor::declare_common(method);
      }
   }
};

} // namespace forge::api::core

FORGE_EXPORT_API(::forge::chain::api::transaction, FORGE_API_CONTRACT("forge.chain.api.transaction", 1, 0),
                 FORGE_API_METHOD_TYPED(get_status, ::forge::chain::protocol::transaction_status_request,
                                        ::forge::chain::protocol::transaction_status_response),
                 FORGE_API_METHOD_TYPED(await_transaction, ::forge::chain::protocol::transaction_await_request,
                                        ::forge::chain::protocol::transaction_status_response),
                 FORGE_API_METHOD_TYPED(get_required_keys, ::forge::chain::protocol::transaction_required_keys_request,
                                        ::std::vector<::forge::chain::protocol::public_key>),
                 FORGE_API_METHOD_TYPED(compute_transaction, ::forge::chain::protocol::transaction_read_only_request,
                                        ::forge::chain::protocol::transaction_read_only_response),
                 FORGE_API_METHOD_TYPED(send_read_only_transaction,
                                        ::forge::chain::protocol::transaction_read_only_request,
                                        ::forge::chain::protocol::transaction_read_only_response))

FORGE_HTTP_API(::forge::chain::api::transaction,
               FORGE_HTTP_GET(get_status, "/v1/chain/transactions/{id}?finality_from={finality_from}&audit={audit}",
                              FORGE_HTTP_CACHE(no_store)),
               FORGE_HTTP_GET(await_transaction,
                              "/v1/chain/transactions/{id}/wait?desired={desired}&timeout_ms={timeout_ms}"
                              "&finality_from={finality_from}&audit={audit}",
                              FORGE_HTTP_CACHE(no_store), FORGE_HTTP_TIMEOUT(timeout_ms)),
               FORGE_HTTP_POST(get_required_keys, "/v1/chain/transactions/required-keys", ok,
                               FORGE_HTTP_CACHE(no_store)),
               FORGE_HTTP_POST(compute_transaction, "/v1/chain/transactions/compute", ok, FORGE_HTTP_CACHE(no_store)),
               FORGE_HTTP_POST(send_read_only_transaction, "/v1/chain/transactions/read-only", ok,
                               FORGE_HTTP_CACHE(no_store)))
