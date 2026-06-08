add_library(
   fcl_yamux STATIC
   session.cpp
)
fcl_target_modules(fcl_yamux yamux)

target_include_directories(fcl_yamux PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_yamux PUBLIC
      fcl_exceptions
      fcl_transport
      Boost::asio
)
