add_library(
   fcl_plugin_http_server STATIC
   plugin.cpp
   details/api_facade.cpp
   details/config.cpp
   details/server_state.cpp
   details/state.cpp
)
fcl_target_modules_at(fcl_plugin_http_server plugins/http_server)

target_include_directories(fcl_plugin_http_server PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_include_directories(fcl_plugin_http_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
   fcl_plugin_http_server PUBLIC
      fcl_app
      fcl_api
      fcl_config
      fcl_schema
      fcl_http
      fcl_asio
      fcl_exceptions
      Boost::asio
)
