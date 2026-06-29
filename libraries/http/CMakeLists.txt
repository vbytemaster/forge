add_library(
   forge_http STATIC
   types.cpp
   base_url.cpp
   target.cpp
   range.cpp
   upload.cpp
   route_context.cpp
   middleware.cpp
   connection.cpp
   client.cpp
   stream.cpp
   router.cpp
   router_server_access.cpp
   stream_server_access.cpp
   file.cpp
   server.cpp
)
forge_target_modules(forge_http http)

target_include_directories(forge_http PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_http PUBLIC
      forge_exceptions
      forge_api
      forge_asio
      forge_json
      forge_schema
      forge_websocket
      Boost::asio
      Boost::beast
      Boost::url
      OpenSSL::SSL
)
