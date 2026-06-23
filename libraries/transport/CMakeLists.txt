add_library(
   forge_transport STATIC
   buffer.cpp
   connector.cpp
   endpoint.cpp
   frame.cpp
   listener.cpp
   registry.cpp
   stream.cpp
   session.cpp
)
forge_target_modules(forge_transport transport)

target_include_directories(forge_transport PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_transport PUBLIC
      forge_exceptions
      Boost::asio
)
