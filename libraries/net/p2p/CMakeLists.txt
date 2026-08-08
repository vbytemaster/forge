add_library(
   forge_net_p2p STATIC
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
   direct_transport_quic.cpp
   direct_transport_tcp.cpp
   libp2p_tls.cpp
   details/libp2p_tls.hxx
   node.cpp
   libp2p_identity_material.cpp
   details/libp2p_identity_material.hxx
   node_impl.cpp
   details/node_impl.hxx
   connection_singleflight_registry.cpp
   details/connection_singleflight_registry.hxx
   operation_deadline.cpp
   details/operation_deadline.hxx
   path_selector.cpp
   details/path_selector.hxx
   peer_exchange_codec.cpp
   details/peer_exchange_codec.hxx
   peer_exchange_learning.cpp
   details/peer_exchange_learning.hxx
   pubsub_outbound_budget.cpp
   details/pubsub_outbound_budget.hxx
   details/peer_failure.hxx
   relay_discovery.cpp
   details/relay_discovery.hxx
   relay_transport.cpp
   details/relay_transport.hxx
   details/dht_query.hxx
   details/protobuf.hxx
   details/protocol_capabilities.hxx
   details/relay_accounting.hxx
   details/session_lifecycle.hxx
   session_teardown.cpp
   details/session_teardown.hxx
   stream_upgrade.cpp
   details/stream_upgrade.hxx
)
forge_target_modules_at(forge_net_p2p net/p2p)

target_include_directories(forge_net_p2p PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(forge_net_p2p PRIVATE FORGE_HAS_ROCKSDB=$<BOOL:${FORGE_HAS_ROCKSDB}>)

target_link_libraries(
   forge_net_p2p PUBLIC
      forge_exceptions
      forge_api_core
      forge_raw
      forge_crypto_asymmetric
      forge_crypto_core
      forge_crypto_digest
      forge_crypto_pki
      forge_crypto_symmetric
      forge_multiformats
      forge_asio
      forge_net_transport
      forge_net_quic
      forge_net_tcp
      forge_net_stcp
      forge_net_yamux
      Boost::asio
)

if(FORGE_HAS_ROCKSDB)
   target_link_libraries(
      forge_net_p2p PRIVATE
         forge_rocksdb
   )
endif()
