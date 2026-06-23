add_library(
   forge_plugins_p2p_pubsub STATIC
      plugin.cpp
      config.cpp
      join_flow.cpp
      message_projection.cpp
      plugin_impl.cpp
      subscription_api.cpp
)
forge_target_modules_at(forge_plugins_p2p_pubsub plugins/p2p/pubsub)

target_include_directories(
   forge_plugins_p2p_pubsub
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_P2P_PUBSUB_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   forge_plugins_p2p_pubsub PUBLIC
      forge_plugins_p2p_node
      forge_app
      forge_api
      forge_p2p
      forge_config
      forge_schema
      forge_raw
      forge_exceptions
      Boost::asio
)
