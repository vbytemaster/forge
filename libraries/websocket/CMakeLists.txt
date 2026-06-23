add_library(
   forge_websocket STATIC
   connection.cpp
   client.cpp
)
forge_target_modules(forge_websocket websocket)

target_include_directories(forge_websocket PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_websocket PUBLIC
      forge_exceptions
      forge_api
      forge_raw
      forge_asio
      Boost::asio
      Boost::beast
      OpenSSL::SSL
)
