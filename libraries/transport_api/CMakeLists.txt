add_library(
   fcl_transport_api STATIC
   client.cpp
   connection.cpp
   server.cpp
)
fcl_target_modules(fcl_transport_api transport_api)

target_include_directories(fcl_transport_api PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_transport_api PUBLIC
      fcl_exceptions
      fcl_api
      fcl_raw
      fcl_transport
      Boost::asio
)
