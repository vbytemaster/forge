add_library(
   forge_plugins_http_server STATIC
   plugin.cpp
   publisher_api.cpp
   middleware_bridge.cpp
   config.cpp
   server_lifecycle.cpp
   plugin_impl.cpp
)
forge_target_modules_at(forge_plugins_http_server plugins/http/server)

target_include_directories(
   forge_plugins_http_server
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_HTTP_SERVER_INCLUDE_DIR}>"
)
target_include_directories(forge_plugins_http_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
   forge_plugins_http_server PUBLIC
      forge_app
      forge_api
      forge_config
      forge_schema
      forge_http
      forge_http_api
      forge_asio
      forge_exceptions
      Boost::asio
)
