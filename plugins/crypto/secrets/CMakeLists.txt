add_library(
   forge_plugins_crypto_secrets STATIC
      plugin.cpp
      secret_api.cpp
      config.cpp
      plugin_impl.cpp
      source_loading.cpp
      encrypted_file.cpp
)
forge_target_modules_at(forge_plugins_crypto_secrets plugins/crypto/secrets)

target_include_directories(
   forge_plugins_crypto_secrets
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_CRYPTO_SECRETS_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   forge_plugins_crypto_secrets PUBLIC
      forge_app
      forge_api
      forge_config
      forge_schema
      forge_crypto
      forge_exceptions
      Boost::asio
)
