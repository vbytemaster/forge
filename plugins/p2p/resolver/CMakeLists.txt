add_library(
   forge_plugins_p2p_resolver STATIC
      plugin.cpp
      config.cpp
      descriptor_projection.cpp
      plugin_impl.cpp
      protocol.cpp
      resolver_api.cpp
)
forge_target_modules_at(forge_plugins_p2p_resolver plugins/p2p/resolver)

target_include_directories(
   forge_plugins_p2p_resolver
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_P2P_RESOLVER_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   forge_plugins_p2p_resolver PUBLIC
      forge_plugins_p2p_node
      forge_app
      forge_api
      forge_transport_api
      forge_p2p
      forge_config
      forge_schema
      forge_exceptions
      Boost::asio
)
