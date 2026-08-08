module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>
#include <forge/api/http/macros.hpp>

#include <vector>

export module forge.chain.api.submission;

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

class submission : public forge::api::core::contract<submission, forge::api::core::surface::local |
                                                                     forge::api::core::surface::remote> {
 public:
   virtual ~submission() = default;

   virtual boost::asio::awaitable<protocol::transaction_submit_response>
   submit(protocol::transaction_submit_request value) = 0;
   virtual boost::asio::awaitable<std::vector<protocol::transaction_submit_response>>
   submit_batch(protocol::transaction_submit_batch_request value) = 0;
};

} // namespace forge::chain::api

export namespace forge::api::core {

template <> struct method_descriptor_customization<::forge::chain::api::submission> {
   template <auto Method, bool EnableRaw>
   static void apply(method_builder<::forge::chain::api::submission, EnableRaw>& method) {
      static_cast<void>(Method);
      ::forge::chain::api::exceptions::descriptor::declare_common(method);
      ::forge::chain::api::exceptions::descriptor::declare_deadline(method);
      ::forge::chain::api::exceptions::descriptor::declare_mutation(method);
   }
};

} // namespace forge::api::core

FORGE_EXPORT_API(::forge::chain::api::submission, FORGE_API_CONTRACT("forge.chain.api.submission", 1, 0),
                 FORGE_API_METHOD_TYPED(submit, ::forge::chain::protocol::transaction_submit_request,
                                        ::forge::chain::protocol::transaction_submit_response),
                 FORGE_API_METHOD_TYPED(submit_batch, ::forge::chain::protocol::transaction_submit_batch_request,
                                        std::vector<::forge::chain::protocol::transaction_submit_response>))

FORGE_HTTP_API(::forge::chain::api::submission,
               FORGE_HTTP_POST(submit, "/v1/chain/transactions/submit", accepted, FORGE_HTTP_TIMEOUT(timeout_ms)),
               FORGE_HTTP_POST(submit_batch, "/v1/chain/transactions/submit-batch", accepted,
                               FORGE_HTTP_TIMEOUT(timeout_ms)))
