module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/macros.hpp>

#include <string>
#include <utility>

export module forge.plugins.crypto.signer.api;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.crypto.sha256;
import forge.plugins.crypto.signer.types;

export namespace forge::plugins::crypto::signer {

class api : public forge::api::contract<api, forge::api::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<response> sign(request value) = 0;

   boost::asio::awaitable<response> sign(std::string key_id, std::string purpose, forge::crypto::sha256 digest) {
      co_return co_await sign(request{
         .key_id = std::move(key_id),
         .purpose = std::move(purpose),
         .digest = digest,
      });
   }

   boost::asio::awaitable<response> sign(std::string key_id, forge::crypto::sha256 digest, options value) {
      co_return co_await sign(request{
         .key_id = std::move(key_id),
         .purpose = std::move(value.purpose),
         .digest = digest,
         .required_algorithm = value.required_algorithm,
         .output_profile = std::move(value.output_profile),
      });
   }
};

} // namespace forge::plugins::crypto::signer

export {
FORGE_API(::forge::plugins::crypto::signer::api, FORGE_API_CONTRACT("forge.plugins.crypto.signer", 1, 0),
        FORGE_API_METHOD_TYPED(sign, ::forge::plugins::crypto::signer::request,
                             ::forge::plugins::crypto::signer::response))
}
