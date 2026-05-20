add_library(
   fcl_p2p STATIC
   errors.cpp
   identity.cpp
   endpoint.cpp
   identify.cpp
   scoring.cpp
   relay.cpp
   stream.cpp
   negotiation.cpp
   codec.cpp
   peer_store.cpp
   node.cpp
)
# TODO(p2p-core-rewrite): Re-enable fcl.p2p.api after bindings target the new
# libp2p-compatible session/stream core.
set(FCL_p2p_DISABLED_MODULES api)
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
