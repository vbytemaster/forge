add_library(
   fcl_p2p STATIC
   identity.cpp
   identity_signature.cpp
   identity_signature.hpp
   endpoint.cpp
   envelope.cpp
   identify.cpp
   dht.cpp
   rendezvous.cpp
   pubsub.cpp
   scoring.cpp
   reachability.cpp
   relay.cpp
   hole_punch.cpp
   resource_manager.cpp
   stream.cpp
   negotiation.cpp
   peer_store.cpp
   direct_transport.cpp
   direct_transport.hpp
   host_addresses.cpp
   host_addresses.hpp
   quic_direct_transport.cpp
   tcp_direct_transport.cpp
   libp2p_tls.cpp
   libp2p_tls.hpp
   node.cpp
   node_impl.cpp
   node_impl.hpp
   operation_deadline.cpp
   operation_deadline.hpp
   path_selector.cpp
   path_selector.hpp
   peer_exchange_codec.cpp
   peer_exchange_codec.hpp
   relay_transport.cpp
   relay_transport.hpp
   session_lifecycle.hpp
   stream_upgrade.cpp
   stream_upgrade.hpp
   tcp_stream_upgrade.hpp
)
fcl_target_modules(fcl_p2p p2p)

target_include_directories(fcl_p2p PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(fcl_p2p PRIVATE FCL_HAS_ROCKSDB=$<BOOL:${FCL_HAS_ROCKSDB}>)

target_link_libraries(
   fcl_p2p PUBLIC
      fcl_exceptions
      fcl_api
      fcl_raw
      fcl_crypto
      fcl_multiformats
      fcl_asio
      fcl_transport
      fcl_quic
      fcl_tcp
      fcl_stcp
      fcl_yamux
      Boost::asio
)

if(FCL_HAS_ROCKSDB)
   target_link_libraries(
      fcl_p2p PRIVATE
         RocksDB::rocksdb
   )
endif()
