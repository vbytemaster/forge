add_library(
   forge_plugins_http_server STATIC
   api_impl.cpp
   bearer_auth.cpp
   config.cpp
   plugin.cpp
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
      forge_api_core
      forge_config_core
      forge_schema
      forge_net_http
      forge_api_http
      forge_asio
      forge_crypto_digest
      forge_exceptions
      Boost::asio
)
