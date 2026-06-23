add_subdirectory(crypto/signer)
add_subdirectory(crypto/secrets)
add_subdirectory(p2p/node)
add_subdirectory(p2p/resolver)
add_subdirectory(p2p/diagnostics)
add_subdirectory(p2p/pubsub)
add_subdirectory(http/server)

add_library(forge_plugins STATIC plugins.cpp)

target_include_directories(forge_plugins PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_plugins PUBLIC
      forge_plugins_crypto_signer
      forge_plugins_crypto_secrets
      forge_plugins_p2p_node
      forge_plugins_p2p_resolver
      forge_plugins_p2p_diagnostics
      forge_plugins_p2p_pubsub
      forge_plugins_http_server
)
