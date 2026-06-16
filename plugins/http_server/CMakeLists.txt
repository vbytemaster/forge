add_library(
   fcl_plugin_http_server STATIC
   plugin.cpp
   api_facade.cpp
   config.cpp
   server_state.cpp
   state.cpp
)
fcl_target_modules_at(fcl_plugin_http_server plugins/http_server)

target_include_directories(
   fcl_plugin_http_server
   PUBLIC
      ${FCL_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FCL_PLUGIN_HTTP_SERVER_INCLUDE_DIR}>"
)
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
