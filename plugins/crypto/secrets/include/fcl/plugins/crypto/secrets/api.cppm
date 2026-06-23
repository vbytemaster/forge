module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/macros.hpp>

export module fcl.plugins.crypto.secrets.api;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.plugins.crypto.secrets.types;

export namespace fcl::plugins::crypto::secrets {

class api : public fcl::api::contract<api, fcl::api::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<snapshot> status(query value) = 0;
   virtual boost::asio::awaitable<get_result> get_bytes(get_request value) = 0;
   virtual boost::asio::awaitable<derive_result> derive_hkdf_sha256(derive_request value) = 0;
   virtual boost::asio::awaitable<aead_encrypt_result> encrypt_aes_gcm(aead_encrypt_request value) = 0;
   virtual boost::asio::awaitable<aead_decrypt_result> decrypt_aes_gcm(aead_decrypt_request value) = 0;
};

} // namespace fcl::plugins::crypto::secrets

export {
FCL_API(::fcl::plugins::crypto::secrets::api, FCL_API_CONTRACT("fcl.plugins.crypto.secrets", 1, 0),
        FCL_API_METHOD_TYPED(status, ::fcl::plugins::crypto::secrets::query,
                             ::fcl::plugins::crypto::secrets::snapshot),
        FCL_API_METHOD_TYPED(get_bytes, ::fcl::plugins::crypto::secrets::get_request,
                             ::fcl::plugins::crypto::secrets::get_result),
        FCL_API_METHOD_TYPED(derive_hkdf_sha256, ::fcl::plugins::crypto::secrets::derive_request,
                             ::fcl::plugins::crypto::secrets::derive_result),
        FCL_API_METHOD_TYPED(encrypt_aes_gcm, ::fcl::plugins::crypto::secrets::aead_encrypt_request,
                             ::fcl::plugins::crypto::secrets::aead_encrypt_result),
        FCL_API_METHOD_TYPED(decrypt_aes_gcm, ::fcl::plugins::crypto::secrets::aead_decrypt_request,
                             ::fcl::plugins::crypto::secrets::aead_decrypt_result))
}
