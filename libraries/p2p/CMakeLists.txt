add_library(
   forge_p2p STATIC
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
   connection_manager.cpp
   connection_manager.hpp
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
   relay_discovery.cpp
   relay_discovery.hpp
   relay_transport.cpp
   relay_transport.hpp
   session_lifecycle.hpp
   stream_upgrade.cpp
   stream_upgrade.hpp
   tcp_stream_upgrade.hpp
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
