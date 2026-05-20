add_library(
   fcl_quic STATIC
   errors.cpp
   endpoint.cpp
   security.cpp
   options.cpp
   libp2p.cpp
   framed_stream.cpp
   runtime.cpp
   quic_engine.cpp
   stream.cpp
   connection.cpp
   connector.cpp
   listener.cpp
)
fcl_target_modules(fcl_quic quic)

target_include_directories(fcl_quic PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_quic PUBLIC
      fcl_exception
      fcl_api
      fcl_raw
      fcl_asio
      Boost::asio
      OpenSSL::SSL
      OpenSSL::Crypto
      Ngtcp2::ngtcp2
      Ngtcp2::crypto_ossl
)
