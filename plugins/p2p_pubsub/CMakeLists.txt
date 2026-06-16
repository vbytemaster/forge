add_library(
   fcl_plugin_p2p_pubsub STATIC
      plugin.cpp
      config.cpp
      join_flow.cpp
      message_projection.cpp
      plugin_impl.cpp
      subscription_api.cpp
)
fcl_target_modules_at(fcl_plugin_p2p_pubsub plugins/p2p_pubsub)

target_include_directories(
   fcl_plugin_p2p_pubsub
   PUBLIC
      ${FCL_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FCL_PLUGIN_P2P_PUBSUB_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   fcl_plugin_p2p_pubsub PUBLIC
      fcl_plugin_p2p_node
      fcl_app
      fcl_api
      fcl_p2p
      fcl_config
      fcl_schema
      fcl_raw
      fcl_exceptions
      Boost::asio
)
