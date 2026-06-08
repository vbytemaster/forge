file(GLOB FCL_CRYPTO_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(fcl_crypto STATIC ${FCL_CRYPTO_SOURCES})
fcl_target_modules(fcl_crypto crypto)

target_include_directories(
   fcl_crypto PUBLIC
      ${FCL_PUBLIC_INCLUDE_DIRS}
)

target_compile_definitions(
   fcl_crypto
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)

target_link_libraries(
   fcl_crypto PUBLIC
      fcl_core
      fcl_exceptions
      fcl_raw
      fcl_reflect
      fcl_variant
      Boost::headers
      GMP::gmp
      OpenSSL::Crypto
      secp256k1
      bls12-381
)
