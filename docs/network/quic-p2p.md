# QUIC + P2P

`fcl_quic` and `fcl_p2p` form the FCL peer data-plane foundation. QUIC is the
secure transport. P2P is the peer/session/protocol-stream layer above it.

Local guides:

- [QUIC README](../../libraries/quic/README.md)
- [P2P README](../../libraries/p2p/README.md)

## Задача

Long-lived peer streams need different semantics than HTTP control APIs:
transport identity, handshake limits, protocol negotiation, direct/relay path
selection, reachability, stream backpressure and deterministic shutdown. Those
concerns need reusable FCL primitives without embedding any product protocol.

## Layering

```text
fcl_asio::runtime
  -> fcl_quic
      -> endpoint/listener/connector
      -> connection/stream/framed_stream
  -> fcl_p2p
      -> peer identity
      -> session/control messages
      -> protocol streams
      -> peer store/path manager/relay
```

QUIC knows endpoints and transport security. P2P knows peers and protocol
streams. Application protocols live above P2P and define their own messages,
durability and authorization.

## QUIC Responsibilities

- UDP socket/timer integration with Asio.
- ngtcp2 packet engine and OpenSSL 3.0+ TLS backend.
- ALPN, certificate verification, pinned fingerprints and mTLS checks.
- Framed stream codec and transport limits.
- Backpressure for streams, queued bytes and inbound packet queues.

QUIC does not own peer discovery, relay policy or application protocol naming.

## P2P Responsibilities

- Local peer identity from transport certificate material.
- Direct connect with expected peer checks.
- Protocol handler registry and stream opening by `protocol_id`.
- Peer exchange and peer store freshness.
- Explicit relay reservation and cancellation.
- Reachability probing and hole-punch attempt orchestration.
- Path scoring/backoff across direct and relay candidates.

P2P does not promise exactly-once delivery, durable storage, global discovery or
product authorization.

## Integration Example

```cpp
auto options = fcl::p2p::node_options{
   .certificate_pem = certificate_pem,
   .private_key_pem = private_key_pem,
};

auto node = fcl::p2p::node{runtime, options};
node.register_protocol_handler(
   fcl::p2p::protocol_id{.value = "/example/1"},
   [](fcl::p2p::incoming_protocol_stream incoming) -> boost::asio::awaitable<void> {
      std::vector<std::uint8_t> frame = co_await incoming.stream.async_read_frame();
      co_await incoming.stream.async_write_frame(frame);
   });

boost::asio::awaitable<void> connect_example(fcl::p2p::node& node) {
   co_await node.async_listen(fcl::quic::parse_endpoint("127.0.0.1:9443"));
   fcl::p2p::session_info session = co_await node.async_connect(remote_endpoint, {.expected_peer = remote_peer});
   fcl::quic::framed_stream stream = co_await node.async_open_protocol_stream(
      session.remote_peer,
      fcl::p2p::protocol_id{.value = "/example/1"});
   use_stream(std::move(stream));
}
```

The protocol ID identifies a stream contract. The protocol still owns its own
message validation, durable semantics and authorization above FCL.

## Failure Model

- A failed direct endpoint is one candidate failure, not necessarily whole
  operation failure while deadline budget remains.
- Peer mismatch and TLS verification failure are correctness failures.
- Oversized or malformed control envelopes are rejected before handler dispatch.
- Relay use is explicit and reservation-backed.
- Non-positive timeouts are rejected early.

## Security Boundary

Peer identity is transport identity. It proves which key/certificate completed
the handshake; it does not authorize product actions. Consumers must still
perform their own authorization and policy checks.

## Donor Decisions

Accepted:

- ngtcp2 transport engine.
- libp2p-style host/protocol separation.
- Circuit Relay style explicit reservation.
- DCUtR-style hole punching as a bounded attempt, not magic connectivity.
- Syncthing/libtorrent-style path scoring/backoff.

Rejected:

- Direct libp2p dependency in v1.
- DHT as mandatory baseline.
- Product storage/application semantics inside P2P.
- Silent insecure peer identity fallback outside tests.

## Verification

`test_fcl_quic_p2p` covers QUIC handshake, frame codec, ALPN and mTLS failures,
pinned fingerprints, direct protocol streams, peer exchange, relay, reachability,
hole punching, malformed envelopes and production option checks.
