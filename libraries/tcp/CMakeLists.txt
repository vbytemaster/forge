add_library(
   forge_tcp STATIC
   connection.cpp
   connector.cpp
   listener.cpp
   transport.cpp
)
forge_target_modules(forge_tcp tcp)

target_include_directories(forge_tcp PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_tcp PUBLIC
      forge_exceptions
      forge_transport
      Boost::asio
)
