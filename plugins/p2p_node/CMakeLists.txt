add_library(
   fcl_plugin_p2p_node STATIC
      plugin.cpp
      details/api_facade.cpp
      details/config.cpp
      details/state.cpp
)
fcl_target_modules_at(fcl_plugin_p2p_node plugins/p2p_node)

target_include_directories(
   fcl_plugin_p2p_node
   PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS}
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
