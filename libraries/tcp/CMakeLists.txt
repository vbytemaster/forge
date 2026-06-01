add_library(
   fcl_tcp STATIC
   connection.cpp
   connector.cpp
   listener.cpp
   transport.cpp
)
fcl_target_modules(fcl_tcp tcp)

target_include_directories(fcl_tcp PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_tcp PUBLIC
      fcl_exceptions
      fcl_transport
      Boost::asio
)
