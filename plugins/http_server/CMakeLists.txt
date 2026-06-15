add_library(
   fcl_plugin_http_server STATIC
   base_path.cpp
   config_decode.cpp
   publication_store.cpp
   server_state.cpp
   plugin.cpp
)
fcl_target_modules_at(fcl_plugin_http_server plugins/http_server)

target_include_directories(fcl_plugin_http_server PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_include_directories(fcl_plugin_http_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
   fcl_plugin_http_server PUBLIC
      fcl_app
      fcl_api
      fcl_config
      fcl_http
      fcl_schema
      fcl_exceptions
      Boost::asio
)
