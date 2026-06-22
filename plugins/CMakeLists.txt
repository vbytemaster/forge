add_subdirectory(signing/provider)
add_subdirectory(secret/provider)
add_subdirectory(p2p/node)
add_subdirectory(p2p/resolver)
add_subdirectory(p2p/diagnostics)
add_subdirectory(p2p/pubsub)
add_subdirectory(http/server)

add_library(fcl_plugins STATIC plugins.cpp)

target_include_directories(fcl_plugins PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_plugins PUBLIC
      fcl_plugins_signing_provider
      fcl_plugins_secret_provider
      fcl_plugins_p2p_node
      fcl_plugins_p2p_resolver
      fcl_plugins_p2p_diagnostics
      fcl_plugins_p2p_pubsub
      fcl_plugins_http_server
)
