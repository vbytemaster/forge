add_library(
   fcl_plugins_p2p_node STATIC
      plugin.cpp
      node_api.cpp
      diagnostics_source.cpp
      pubsub_source.cpp
      config.cpp
      plugin_impl.cpp
)
fcl_target_modules_at(fcl_plugins_p2p_node plugins/p2p/node)

target_include_directories(
   fcl_plugins_p2p_node
   PUBLIC
      ${FCL_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FCL_PLUGINS_P2P_NODE_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   fcl_plugins_p2p_node PUBLIC
      fcl_app
      fcl_api
      fcl_transport_api
      fcl_p2p
      fcl_config
      fcl_schema
      fcl_asio
      fcl_exceptions
      Boost::asio
)
