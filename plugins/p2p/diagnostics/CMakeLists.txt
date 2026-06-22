add_library(
   fcl_plugins_p2p_diagnostics STATIC
      plugin.cpp
      diagnostics_api.cpp
      config.cpp
      plugin_impl.cpp
)
fcl_target_modules_at(fcl_plugins_p2p_diagnostics plugins/p2p/diagnostics)

target_include_directories(
   fcl_plugins_p2p_diagnostics
   PUBLIC
      ${FCL_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FCL_PLUGINS_P2P_DIAGNOSTICS_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   fcl_plugins_p2p_diagnostics PUBLIC
      fcl_plugins_p2p_node
      fcl_app
      fcl_api
      fcl_p2p
      fcl_config
      fcl_schema
      fcl_exceptions
      Boost::asio
)
