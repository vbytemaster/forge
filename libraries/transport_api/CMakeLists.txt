add_library(
   forge_transport_api STATIC
   client.cpp
   connection.cpp
   server.cpp
)
forge_target_modules(forge_transport_api transport_api)

target_include_directories(forge_transport_api PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_transport_api PUBLIC
      forge_exceptions
      forge_api
      forge_raw
      forge_transport
      Boost::asio
)
