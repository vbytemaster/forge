add_subdirectory(signature_provider)
add_subdirectory(p2p_node)
add_subdirectory(p2p_api_resolver)
add_subdirectory(p2p_diagnostics)
add_subdirectory(p2p_pubsub)

add_library(fcl_plugins STATIC plugins.cpp)

target_include_directories(fcl_plugins PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_plugins PUBLIC
      fcl_plugin_signature_provider
      fcl_plugin_p2p_node
      fcl_plugin_p2p_api_resolver
      fcl_plugin_p2p_diagnostics
      fcl_plugin_p2p_pubsub
)
