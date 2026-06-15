add_library(
   fcl_http STATIC
   base_url.cpp
   target.cpp
   range.cpp
   upload.cpp
   route_context.cpp
   middleware.cpp
   connection.cpp
   client.cpp
   router.cpp
   file.cpp
   server.cpp
)
fcl_target_modules(fcl_http http)

target_include_directories(fcl_http PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_http PUBLIC
      fcl_exceptions
      fcl_api
      fcl_asio
      fcl_websocket
      Boost::asio
      Boost::beast
      Boost::url
      OpenSSL::SSL
)
