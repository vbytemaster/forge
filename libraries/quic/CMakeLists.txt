add_library(
   fcl_quic STATIC
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
fcl_target_modules(fcl_quic quic)

target_include_directories(fcl_quic PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_quic PUBLIC
      fcl_exceptions
      fcl_api
      fcl_api_transport
      fcl_raw
      fcl_crypto
      fcl_asio
      fcl_transport
      Boost::asio
      OpenSSL::SSL
      OpenSSL::Crypto
      Ngtcp2::ngtcp2
      Ngtcp2::crypto_ossl
)
