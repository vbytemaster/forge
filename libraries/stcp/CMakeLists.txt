add_library(
   forge_stcp STATIC
   connection.cpp
   connector.cpp
   listener.cpp
)
forge_target_modules(forge_stcp stcp)

target_include_directories(forge_stcp PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_stcp PUBLIC
      forge_exceptions
      forge_transport
      forge_tcp
      forge_crypto
      Boost::asio
      OpenSSL::SSL
)
