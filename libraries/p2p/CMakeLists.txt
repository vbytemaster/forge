add_library(
   fcl_p2p STATIC
   errors.cpp
   identity.cpp
   scoring.cpp
   relay.cpp
   codec.cpp
   peer_store.cpp
   options.cpp
   session.cpp
   node.cpp
)
fcl_target_modules(fcl_p2p p2p)

target_include_directories(fcl_p2p PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_p2p PUBLIC
      fcl_asio
      fcl_quic
      Boost::asio
)
