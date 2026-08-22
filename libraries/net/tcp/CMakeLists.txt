add_library(
   forge_net_tcp STATIC
   connection.cpp
   connector.cpp
   listener.cpp
   transport.cpp
)
forge_target_modules_at(forge_net_tcp net/tcp)

target_include_directories(forge_net_tcp PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_net_tcp PUBLIC
      forge_exceptions
      forge_net_transport
      Boost::asio
   PRIVATE
      forge_asio
)
