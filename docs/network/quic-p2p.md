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

P2P does not promise exactly-once delivery, durable storage or product
authorization. Global discovery is not implemented yet, but it is a
network-level direction for `fcl_p2p`, not a plugin-level workaround.

For application/plugin composition, `fcl::plugins::p2p_node` is the production
transport owner above `fcl_p2p`. It applies config, starts the node, mounts
protocol/API contributions, exposes safe send/broadcast APIs and integrates an
optional pluggable outbox store. Product plugins should not create a second node
or run ad hoc retry loops against raw `fcl::p2p::node`.

## Production P2P Direction

`fcl_p2p` targets a clean C++23 implementation of a libp2p-compatible network
stack. Compatibility means protocol compatibility (`протокольная
совместимость`): when FCL declares support for a libp2p protocol, an FCL node
must be able to talk to go-libp2p and rust-libp2p nodes using the same wire
format, handshake, peer identity, protocol negotiation and message rules.

This does not mean copying Go/Rust runtime architecture or leaking libp2p's
public vocabulary into FCL. FCL public APIs should keep Boost-style network
terms such as `endpoint`, `resolver`, `listener`, `connector`, `session`,
`stream` and `protocol_id`. Terms such as `multiaddr` describe the libp2p
wire/text address format; FCL can expose a typed endpoint model that reads and
writes that format without making `multiaddr` the public API style.

Production network mechanics belong in `fcl_p2p`:

- A typed endpoint/address model for direct, observed and relayed paths, with
  libp2p multiaddress read/write support.
- Peer ID and key encoding compatible with libp2p. Ed25519, Secp256k1, ECDSA
  and RSA are all part of the compatibility baseline.
- Protocol negotiation compatible with libp2p multistream-select.
- Identify and Identify Push compatible with libp2p peer and capability
  advertisement: peer id, public key, supported protocols, addresses,
  agent/version and limits.
- Ping compatible with libp2p interop test plans.
- Persistent peer/path store for endpoints, relay candidates, protocol support,
  signed peer records, scores, backoff and expiry. The store is interface-based;
  RocksDB is the default production backend.
- AutoNAT-style reachability service, not just one-off probes.
- Circuit Relay style relay manager with reservation, TTL, renewal, limits and
  accounting.
- AutoRelay-style relay discovery, reservation selection and advertised relayed
  addresses.
- DCUtR-style direct connection upgrade through relay and hardened hole-punch
  orchestration.
- DHT/rendezvous discovery and pubsub/gossip as later network services.
- Network limits, backpressure, metrics and deterministic shutdown for all of
  the above.

Implementation should start from the foundation in this order:

```text
multiformats + byte-friendly base58
  -> Peer ID + key encoding
  -> endpoint/address compatibility
  -> QUIC libp2p profile
  -> multistream-select
  -> Ping
  -> Identify / Identify Push
  -> persistent peer/path store
  -> AutoNAT / reachability
  -> Circuit Relay / AutoRelay
  -> DCUtR hardening
  -> DHT / rendezvous
  -> pubsub / gossip
```

AutoNAT, AutoRelay, DHT and pubsub algorithms must live in `fcl_p2p`.
`fcl::plugins::p2p_node` configures and runs the shared node, then exposes the
network capabilities through narrow application APIs. If a network behavior is
not implemented yet, expose a typed unsupported/limited result instead of hiding
the gap above the network layer.

## Donor Test Adoption

libp2p specs, go-libp2p and rust-libp2p are not just inspiration; they are the
compatibility contract for protocols FCL claims to support. FCL should not copy
Go/Rust runtime code, but it must adopt their protocol criteria, golden vectors,
failure cases and interop scenarios.

For each supported libp2p protocol, keep a traceability matrix:

| Protocol | Spec source | Donor tests inspected | FCL unit tests | FCL interop tests | Unsupported gaps |
| --- | --- | --- | --- | --- | --- |
| Multiformats / Peer ID | `libp2p-specs` | go-libp2p/rust-libp2p identity tests | golden byte vectors | not required | listed explicitly |
| QUIC + Ping | `libp2p-specs/quic`, `libp2p-specs/ping` | go-libp2p `test-plans`, rust-libp2p interop tests | FCL transport tests | FCL <-> Go/Rust in both directions | listed explicitly |
| Identify | `libp2p-specs/identify` | go-libp2p/rust-libp2p Identify tests | FCL encode/decode and peerstore tests | FCL <-> Go/Rust Identify exchange | listed explicitly |

Test layers:

- `golden`: byte-level vectors for varint, multicodec, multihash, multibase,
  Peer ID, signed records and Identify messages.
- `component`: FCL-to-FCL tests for endpoint parsing, negotiation, Ping,
  Identify and peer/path store behavior.
- `interop`: FCL client/server against go-libp2p and rust-libp2p in both
  directions.
- `plugin/system`: realistic scenarios through `fcl::plugins::p2p_node` and
  small focused friend plugins, not a parallel fake test runtime.
- `performance/stability`: latency, throughput, long sessions, reconnect, many
  peers, backpressure and peerstore recovery.

If the libp2p ecosystem already has an acceptance criterion, the FCL test must
reference that criterion. A test that is only "similar to libp2p" is not enough
to mark a protocol as supported.

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
- `async_connect(...)` establishes a direct session to a concrete endpoint.
  Direct -> hole punch -> relay fallback happens when opening a protocol stream,
  and the plugin turns that engine behavior into delivery policy.

## Security Boundary

Peer identity is transport identity. It proves which key/certificate completed
the handshake; it does not authorize product actions. Consumers must still
perform their own authorization and policy checks.

## Donor Decisions

Accepted:

- ngtcp2 transport engine.
- libp2p-compatible host/protocol separation without adopting Go/Rust runtime
  architecture.
- libp2p-compatible Identify and Ping.
- libp2p multiaddress as a compatibility format behind an FCL typed endpoint
  model.
- libp2p Peer ID, multiformats, key encoding and multistream-select as wire
  compatibility requirements.
- Circuit Relay style explicit reservation.
- AutoNAT/AutoRelay as network services, not product/plugin loops.
- DCUtR-style hole punching as a bounded attempt, not magic connectivity.
- Kademlia DHT and rendezvous as future discovery donors.
- GossipSub as a future pubsub/gossip donor.
- Syncthing/libtorrent-style path scoring/backoff.
- Transactional outbox style durable retry as an application/plugin-level
  pattern, not a storage dependency inside `fcl_p2p`.

Rejected:

- Direct libp2p dependency in v1.
- Go/Rust runtime style as FCL public architecture.
- Free-form tests that do not map to libp2p specs, donor tests or interop
  criteria.
- Product storage/application semantics inside P2P.
- Product authorization or business acknowledgement inside P2P.
- Silent insecure peer identity fallback outside tests.

## Verification

`test_fcl_quic_p2p` covers QUIC handshake, frame codec, ALPN and mTLS failures,
pinned fingerprints, direct protocol streams, peer exchange, relay, reachability,
hole punching, malformed envelopes and production option checks.
