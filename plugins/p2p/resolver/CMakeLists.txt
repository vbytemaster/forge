add_library(
   fcl_plugins_p2p_resolver STATIC
      plugin.cpp
      config.cpp
      descriptor_projection.cpp
      plugin_impl.cpp
      protocol.cpp
      resolver_api.cpp
)
fcl_target_modules_at(fcl_plugins_p2p_resolver plugins/p2p/resolver)

target_include_directories(
   fcl_plugins_p2p_resolver
   PUBLIC
      ${FCL_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FCL_PLUGINS_P2P_RESOLVER_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   fcl_plugins_p2p_resolver PUBLIC
      fcl_plugins_p2p_node
      fcl_app
      fcl_api
      fcl_transport_api
      fcl_p2p
      fcl_config
      fcl_schema
      fcl_exceptions
      Boost::asio
)
