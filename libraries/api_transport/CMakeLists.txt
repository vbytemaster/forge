add_library(
   fcl_api_transport STATIC
   client.cpp
   connection.cpp
   server.cpp
)
fcl_target_modules(fcl_api_transport api_transport)

target_include_directories(fcl_api_transport PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_api_transport PUBLIC
      fcl_exceptions
      fcl_api
      fcl_raw
      fcl_transport
      Boost::asio
)
