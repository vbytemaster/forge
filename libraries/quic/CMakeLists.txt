add_library(
   forge_quic STATIC
   endpoint.cpp
   security.cpp
   options.cpp
   libp2p.cpp
   framed_stream.cpp
   runtime.cpp
   quic_engine.cpp
   stream.cpp
   connection.cpp
   transport.cpp
   connector.cpp
   listener.cpp
)
forge_target_modules(forge_quic quic)

target_include_directories(forge_quic PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_quic PUBLIC
      forge_exceptions
      forge_api
      forge_transport_api
      forge_raw
      forge_crypto
      forge_asio
      forge_transport
      Boost::asio
      OpenSSL::SSL
      OpenSSL::Crypto
      Ngtcp2::ngtcp2
      Ngtcp2::crypto_ossl
)
