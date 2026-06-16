add_library(
   fcl_plugin_p2p_node STATIC
      plugin.cpp
      api_facade.cpp
      config.cpp
      state.cpp
)
fcl_target_modules_at(fcl_plugin_p2p_node plugins/p2p_node)

target_include_directories(
   fcl_plugin_p2p_node
   PUBLIC
      ${FCL_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FCL_PLUGIN_P2P_NODE_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   fcl_plugin_p2p_node PUBLIC
      fcl_app
      fcl_api
      fcl_api_transport
      fcl_p2p
      fcl_config
      fcl_schema
      fcl_asio
      fcl_exceptions
      Boost::asio
)
