module;

#include <cstdint>
#include <string>

export module forge.quic.runtime;

export namespace forge::quic {

struct runtime_capabilities {
   std::string ngtcp2_version;
   std::string tls_backend = "openssl";
   std::uint32_t openssl_version_major = 0;
   std::uint32_t openssl_version_minor = 0;
   std::uint32_t openssl_version_patch = 0;
   bool crypto_ossl_initialized = false;
};

[[nodiscard]] runtime_capabilities initialize_runtime();

} // namespace forge::quic
