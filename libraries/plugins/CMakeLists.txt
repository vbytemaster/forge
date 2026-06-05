add_library(
   fcl_plugins STATIC
   p2p_node.cpp
)
fcl_target_modules(fcl_plugins plugins)

target_include_directories(fcl_plugins PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_plugins PUBLIC
      fcl_app
      fcl_api
      fcl_api_transport
      fcl_p2p
      fcl_config
      fcl_asio
      Boost::asio
)
