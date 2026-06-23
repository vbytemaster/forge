module;

#include <mutex>
#include <string>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/opensslv.h>

module forge.quic.runtime;

namespace forge::quic {
namespace {

std::once_flag init_once;
bool init_ok = false;

} // namespace

runtime_capabilities initialize_runtime() {
   std::call_once(init_once, [] { init_ok = ngtcp2_crypto_ossl_init() == 0; });

   return runtime_capabilities{
       .ngtcp2_version = NGTCP2_VERSION,
       .tls_backend = "openssl",
       .openssl_version_major = OPENSSL_VERSION_MAJOR,
       .openssl_version_minor = OPENSSL_VERSION_MINOR,
       .openssl_version_patch = OPENSSL_VERSION_PATCH,
       .crypto_ossl_initialized = init_ok,
   };
}

} // namespace forge::quic
