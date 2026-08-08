add_library(forge_crypto_asymmetric_values STATIC)
target_sources(
   forge_crypto_asymmetric_values
   PUBLIC
      FILE_SET CXX_MODULES
      BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
      FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/forge/crypto/asymmetric/values.cppm"
)
target_compile_features(forge_crypto_asymmetric_values PUBLIC cxx_std_23)
target_include_directories(forge_crypto_asymmetric_values PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(forge_crypto_asymmetric_values PUBLIC forge_raw)
forge_target_contract_guest_component(
   forge_crypto_asymmetric_values
   ID forge.crypto.asymmetric_values
)

add_library(
   forge_crypto_asymmetric STATIC
   asymmetric.cpp
   asymmetric_encoding.cpp
   ed25519.cpp
   p256.cpp
   private_key_impl.cpp
   rsa.cpp
   serialization.cpp
   secp256k1.cpp
   secp256k1_backend.cpp
   secp256k1_common.cpp
   secp256k1_recover.cpp
   webauthn.cpp
   x25519.cpp
)
set(FORGE_crypto_asymmetric_DISABLED_MODULES values)
forge_target_modules_at(forge_crypto_asymmetric crypto/asymmetric)
unset(FORGE_crypto_asymmetric_DISABLED_MODULES)

target_include_directories(forge_crypto_asymmetric PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(
   forge_crypto_asymmetric
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
target_link_libraries(
   forge_crypto_asymmetric
   PUBLIC
      forge_codec_base64
      forge_codec_base58
      forge_codec_hex
      forge_core
      forge_crypto_asymmetric_values
      forge_crypto_core
      forge_crypto_digest
      forge_exceptions
      forge_raw
      forge_variant
      Boost::headers
   PRIVATE
      OpenSSL::Crypto
      secp256k1
)
forge_target_contract_guest_component(forge_crypto_asymmetric ID forge.crypto.asymmetric)
