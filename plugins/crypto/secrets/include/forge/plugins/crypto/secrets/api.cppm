module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/macros.hpp>

export module forge.plugins.crypto.secrets.api;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.plugins.crypto.secrets.types;

export namespace forge::plugins::crypto::secrets {

class api : public forge::api::contract<api, forge::api::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<snapshot> status(query value) = 0;
   virtual boost::asio::awaitable<get_result> get_bytes(get_request value) = 0;
   virtual boost::asio::awaitable<derive_result> derive_hkdf_sha256(derive_request value) = 0;
   virtual boost::asio::awaitable<aead_encrypt_result> encrypt_aes_gcm(aead_encrypt_request value) = 0;
   virtual boost::asio::awaitable<aead_decrypt_result> decrypt_aes_gcm(aead_decrypt_request value) = 0;
};

} // namespace forge::plugins::crypto::secrets

export {
FORGE_API(::forge::plugins::crypto::secrets::api, FORGE_API_CONTRACT("forge.plugins.crypto.secrets", 1, 0),
        FORGE_API_METHOD_TYPED(status, ::forge::plugins::crypto::secrets::query,
                             ::forge::plugins::crypto::secrets::snapshot),
        FORGE_API_METHOD_TYPED(get_bytes, ::forge::plugins::crypto::secrets::get_request,
                             ::forge::plugins::crypto::secrets::get_result),
        FORGE_API_METHOD_TYPED(derive_hkdf_sha256, ::forge::plugins::crypto::secrets::derive_request,
                             ::forge::plugins::crypto::secrets::derive_result),
        FORGE_API_METHOD_TYPED(encrypt_aes_gcm, ::forge::plugins::crypto::secrets::aead_encrypt_request,
                             ::forge::plugins::crypto::secrets::aead_encrypt_result),
        FORGE_API_METHOD_TYPED(decrypt_aes_gcm, ::forge::plugins::crypto::secrets::aead_decrypt_request,
                             ::forge::plugins::crypto::secrets::aead_decrypt_result))
}
