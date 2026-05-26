add_library(
   fcl_p2p STATIC
   identity.cpp
   endpoint.cpp
   envelope.cpp
   identify.cpp
   scoring.cpp
   reachability.cpp
   relay.cpp
   hole_punch.cpp
   resource_manager.cpp
   stream.cpp
   negotiation.cpp
   peer_store.cpp
   node.cpp
)
fcl_target_modules(fcl_p2p p2p)

target_include_directories(fcl_p2p PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_p2p PUBLIC
      fcl_exception
      fcl_api
      fcl_raw
      fcl_crypto
      fcl_multiformats
      fcl_asio
      fcl_quic
      Boost::asio
)

target_link_libraries(
   fcl_p2p PRIVATE
      RocksDB::rocksdb
)
