add_library(
   forge_yamux STATIC
   session.cpp
)
forge_target_modules(forge_yamux yamux)

target_include_directories(forge_yamux PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_yamux PUBLIC
      forge_exceptions
      forge_transport
      Boost::asio
)
