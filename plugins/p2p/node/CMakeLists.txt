add_library(
   forge_plugins_p2p_node STATIC
      plugin.cpp
      node_api.cpp
      diagnostics_source.cpp
      pubsub_source.cpp
      config.cpp
      plugin_impl.cpp
)
forge_target_modules_at(forge_plugins_p2p_node plugins/p2p/node)

target_include_directories(
   forge_plugins_p2p_node
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_P2P_NODE_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   forge_plugins_p2p_node PUBLIC
      forge_app
      forge_api
      forge_transport_api
      forge_p2p
      forge_config
      forge_schema
      forge_asio
      forge_exceptions
      Boost::asio
)
