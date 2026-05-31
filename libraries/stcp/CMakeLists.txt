add_library(
   fcl_stcp STATIC
   connection.cpp
   connector.cpp
   listener.cpp
)
fcl_target_modules(fcl_stcp stcp)

target_include_directories(fcl_stcp PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_stcp PUBLIC
      fcl_exception
      fcl_transport
      fcl_tcp
      fcl_crypto
      Boost::asio
      OpenSSL::SSL
)
