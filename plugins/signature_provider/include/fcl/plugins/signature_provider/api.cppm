module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/api_macros.hpp>

#include <string>
#include <utility>

export module fcl.plugins.signature_provider.api;

import fcl.api;
import fcl.crypto.sha256;
import fcl.plugins.signature_provider.types;

export namespace fcl::plugins::signature_provider {

class api : public fcl::api::contract<api, fcl::api::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<response> sign(request value) = 0;

   boost::asio::awaitable<response> sign(std::string key_id, std::string purpose, fcl::crypto::sha256 digest) {
      co_return co_await sign(request{
         .key_id = std::move(key_id),
         .purpose = std::move(purpose),
         .digest = digest,
      });
   }

   boost::asio::awaitable<response> sign(std::string key_id, fcl::crypto::sha256 digest, options value) {
      co_return co_await sign(request{
         .key_id = std::move(key_id),
         .purpose = std::move(value.purpose),
         .digest = digest,
         .required_algorithm = value.required_algorithm,
         .output_profile = std::move(value.output_profile),
      });
   }
};

} // namespace fcl::plugins::signature_provider

export {
FCL_API(::fcl::plugins::signature_provider::api, FCL_API_CONTRACT("fcl.plugins.signature_provider", 1, 0),
        FCL_API_METHOD_TYPED(sign, ::fcl::plugins::signature_provider::request,
                             ::fcl::plugins::signature_provider::response))
}
