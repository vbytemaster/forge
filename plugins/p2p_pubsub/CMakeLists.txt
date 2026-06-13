add_library(fcl_plugin_p2p_pubsub STATIC plugin.cpp)
fcl_target_modules_at(fcl_plugin_p2p_pubsub plugins/p2p_pubsub)

target_include_directories(fcl_plugin_p2p_pubsub PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

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
