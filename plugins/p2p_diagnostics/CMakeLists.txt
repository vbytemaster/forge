add_library(
   fcl_plugin_p2p_diagnostics STATIC
      plugin.cpp
      details/api_facade.cpp
      details/config.cpp
      details/state.cpp
)
fcl_target_modules_at(fcl_plugin_p2p_diagnostics plugins/p2p_diagnostics)

target_include_directories(
   fcl_plugin_p2p_diagnostics
   PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS}
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   fcl_plugin_p2p_diagnostics PUBLIC
      fcl_plugin_p2p_node
      fcl_app
      fcl_api
      fcl_p2p
      fcl_config
      fcl_schema
      fcl_exceptions
      Boost::asio
)
