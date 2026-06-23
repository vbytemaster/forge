add_library(
   forge_plugins_crypto_signer STATIC
      plugin.cpp
      signer_api.cpp
      config.cpp
      plugin_impl.cpp
)
forge_target_modules_at(forge_plugins_crypto_signer plugins/crypto/signer)

target_include_directories(
   forge_plugins_crypto_signer
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_CRYPTO_SIGNER_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   forge_plugins_crypto_signer PUBLIC
      forge_app
      forge_api
      forge_config
      forge_schema
      forge_crypto
      forge_exceptions
      Boost::asio
)
