add_library(
   fcl_transport STATIC
   endpoint.cpp
   frame.cpp
   stream.cpp
   session.cpp
)
fcl_target_modules(fcl_transport transport)

target_include_directories(fcl_transport PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_transport PUBLIC
      fcl_exception
      Boost::asio
)
