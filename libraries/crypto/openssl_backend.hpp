#pragma once

#include <openssl/bn.h>
#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>

namespace forge::crypto::detail {

template <typename ssl_type> struct ssl_wrapper {
   explicit ssl_wrapper(ssl_type* obj) : obj(obj) {}

   operator ssl_type*() {
      return obj;
   }
   operator const ssl_type*() const {
      return obj;
   }
   ssl_type* operator->() {
      return obj;
   }
   const ssl_type* operator->() const {
      return obj;
   }

   ssl_type* obj;
};

#define FORGE_CRYPTO_SSL_TYPE(name, ssl_type, free_func)                                                                \
   struct name : public ssl_wrapper<ssl_type> {                                                                       \
      explicit name(ssl_type* obj = nullptr) : ssl_wrapper(obj) {}                                                     \
      ~name() {                                                                                                       \
         if (obj != nullptr)                                                                                          \
            free_func(obj);                                                                                           \
      }                                                                                                               \
   };

FORGE_CRYPTO_SSL_TYPE(ec_group, EC_GROUP, EC_GROUP_free)
FORGE_CRYPTO_SSL_TYPE(ec_point, EC_POINT, EC_POINT_free)
FORGE_CRYPTO_SSL_TYPE(ecdsa_sig, ECDSA_SIG, ECDSA_SIG_free)
FORGE_CRYPTO_SSL_TYPE(bn_ctx, BN_CTX, BN_CTX_free)
FORGE_CRYPTO_SSL_TYPE(evp_cipher_ctx, EVP_CIPHER_CTX, EVP_CIPHER_CTX_free)

#undef FORGE_CRYPTO_SSL_TYPE

struct ssl_bignum : public ssl_wrapper<BIGNUM> {
   ssl_bignum() : ssl_wrapper(BN_new()) {}
   ~ssl_bignum() {
      BN_free(obj);
   }
};

} // namespace forge::crypto::detail
