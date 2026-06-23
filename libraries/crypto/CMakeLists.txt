file(GLOB FORGE_CRYPTO_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(forge_crypto STATIC ${FORGE_CRYPTO_SOURCES})
forge_target_modules(forge_crypto crypto)

target_include_directories(
   forge_crypto PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
)

target_compile_definitions(
   forge_crypto
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)

target_link_libraries(
   forge_crypto PUBLIC
      forge_core
      forge_exceptions
      forge_raw
      forge_reflect
      forge_variant
      Boost::headers
      GMP::gmp
      OpenSSL::Crypto
      secp256k1
      bls12-381
)
