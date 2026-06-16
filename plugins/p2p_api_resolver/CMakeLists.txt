add_library(
   fcl_plugin_p2p_api_resolver STATIC
      plugin.cpp
      details/implementation.cpp
)
fcl_target_modules_at(fcl_plugin_p2p_api_resolver plugins/p2p_api_resolver)

target_include_directories(
   fcl_plugin_p2p_api_resolver
   PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS}
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   fcl_plugin_p2p_api_resolver PUBLIC
      fcl_plugin_p2p_node
      fcl_app
      fcl_api
      fcl_api_transport
      fcl_p2p
      fcl_config
      fcl_schema
      fcl_exceptions
      Boost::asio
)
