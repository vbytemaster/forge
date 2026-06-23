add_library(
   forge_plugins_p2p_diagnostics STATIC
      plugin.cpp
      diagnostics_api.cpp
      config.cpp
      plugin_impl.cpp
)
forge_target_modules_at(forge_plugins_p2p_diagnostics plugins/p2p/diagnostics)

target_include_directories(
   forge_plugins_p2p_diagnostics
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_P2P_DIAGNOSTICS_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   forge_plugins_p2p_diagnostics PUBLIC
      forge_plugins_p2p_node
      forge_app
      forge_api
      forge_p2p
      forge_config
      forge_schema
      forge_exceptions
      Boost::asio
)
