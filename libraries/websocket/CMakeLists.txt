add_library(
   fcl_websocket STATIC
   connection.cpp
   client.cpp
)
fcl_target_modules(fcl_websocket websocket)

target_include_directories(fcl_websocket PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_websocket PUBLIC
      fcl_exceptions
      fcl_api
      fcl_raw
      fcl_asio
      Boost::asio
      Boost::beast
      OpenSSL::SSL
)
