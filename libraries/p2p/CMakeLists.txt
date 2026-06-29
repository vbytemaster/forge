add_library(
   forge_p2p STATIC
   identity.cpp
   identity_signature.cpp
   details/identity_signature.hxx
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
   connection_manager.cpp
   details/connection_manager.hxx
   resource_manager.cpp
   stream.cpp
   negotiation.cpp
   peer_store.cpp
   direct_transport.cpp
   details/direct_transport.hxx
   host_addresses.cpp
   details/host_addresses.hxx
   quic_direct_transport.cpp
   tcp_direct_transport.cpp
   libp2p_tls.cpp
   details/libp2p_tls.hxx
   node.cpp
   node_impl.cpp
   details/node_impl.hxx
   operation_deadline.cpp
   details/operation_deadline.hxx
   path_selector.cpp
   details/path_selector.hxx
   peer_exchange_codec.cpp
   details/peer_exchange_codec.hxx
   relay_discovery.cpp
   details/relay_discovery.hxx
   relay_transport.cpp
   details/relay_transport.hxx
   details/dht_query.hxx
   details/protobuf.hxx
   details/protocol_capabilities.hxx
   details/relay_accounting.hxx
   details/session_lifecycle.hxx
   stream_upgrade.cpp
   details/stream_upgrade.hxx
)
forge_target_modules(forge_p2p p2p)

target_include_directories(forge_p2p PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(forge_p2p PRIVATE FORGE_HAS_ROCKSDB=$<BOOL:${FORGE_HAS_ROCKSDB}>)

target_link_libraries(
   forge_p2p PUBLIC
      forge_exceptions
      forge_api
      forge_transport_api
      forge_raw
      forge_crypto
      forge_multiformats
      forge_asio
      forge_transport
      forge_quic
      forge_tcp
      forge_stcp
      forge_yamux
      Boost::asio
)

if(FORGE_HAS_ROCKSDB)
   target_link_libraries(
      forge_p2p PRIVATE
         forge_rocksdb
   )
endif()
