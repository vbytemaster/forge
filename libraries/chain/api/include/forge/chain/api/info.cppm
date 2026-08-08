module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/core/macros.hpp>
#include <forge/api/http/macros.hpp>

export module forge.chain.api.info;

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
import forge.crypto.digest.sha256;
import forge.net.http.types;

export import forge.chain.api.exceptions;
export import forge.chain.protocol.info;

export namespace forge::chain::api {

class info
    : public forge::api::core::contract<info, forge::api::core::surface::local | forge::api::core::surface::remote> {
 public:
   virtual ~info() = default;

   virtual boost::asio::awaitable<protocol::info_response> get(protocol::anchored_request value) = 0;
};

} // namespace forge::chain::api

export namespace forge::api::core {

template <> struct method_descriptor_customization<::forge::chain::api::info> {
   template <auto Method, bool EnableRaw>
   static void apply(method_builder<::forge::chain::api::info, EnableRaw>& method) {
      ::forge::chain::api::exceptions::descriptor::declare_audited_query<Method>(method);
   }
};

} // namespace forge::api::core

FORGE_EXPORT_API(::forge::chain::api::info, FORGE_API_CONTRACT("forge.chain.api.info", 1, 0),
                 FORGE_API_METHOD_TYPED(get, ::forge::chain::protocol::anchored_request,
                                        ::forge::chain::protocol::info_response))

FORGE_HTTP_API(::forge::chain::api::info,
               FORGE_HTTP_GET(get, "/v1/chain/info?anchor={anchor}&finality_from={finality_from}&audit={audit}",
                              FORGE_HTTP_CACHE(no_store)))
