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
formats, handshake, Peer ID, Identify, Ping, QUIC profile and later supported
protocol rules.

This does not mean copying Go/Rust runtime architecture or leaking libp2p's
public vocabulary into FCL. FCL public APIs should keep Boost-style network
terms such as `endpoint`, `resolver`, `listener`, `connector`, `session`,
`stream` and `protocol_id`. Terms such as `multiaddr` describe the libp2p
wire/text address format; FCL can expose a typed endpoint model that reads and
writes that format without making `multiaddr` the public API style.

Production network mechanics belong in `fcl_p2p`, not in plugin-local
workarounds: identity, keys, endpoint/address encoding, protocol negotiation,
Identify, Ping, peer/path store, relay, AutoNAT, DHT and pubsub. The
`fcl::plugins::p2p_node` plugin only maps config into the node, owns application
lifecycle, mounts route/API contributions and exposes safe application APIs.
Product plugins must not build their own discovery, relay, DHT or pubsub loops.

Ed25519, Secp256k1, ECDSA and RSA are all mandatory compatibility key families.
RSA is required for IPFS/mainline DHT compatibility. Secp256k1 and ECDSA are
required for blockchain-like networks built on top of FCL and plugins.

`fcl.crypto.base58` must be cleaned up before multiformats code depends on it:
new APIs use `std::span<const std::uint8_t>` and `std::vector<std::uint8_t>`,
while old `char` / `std::vector<char>` overloads remain compatibility wrappers.
Multiformats code should use byte-native APIs without scattered casts.

## Implementation Blocks

### Блок 1: foundation compatibility

- multiformats: varint, multicodec, multihash, multibase, base58btc and base32.
- Peer ID and key encoding according to libp2p specs.
- FCL public `p2p::endpoint` with read/write support for the libp2p multiaddress
  format.
- QUIC libp2p profile: ALPN `libp2p`, `/quic-v1`, connection/session semantics.

### Блок 2: first real interop

- multistream-select.
- Ping.
- Identify and Identify Push.
- Persistent peer/path store through an interface, with RocksDB as the default
  backend.
- `p2p_node` receives only config knobs, not its own network algorithms.

### Блок 3: production networking

- relay v2, AutoNAT, AutoRelay and DCUtR hole punching.
- DHT/rendezvous discovery.
- GossipSub/pubsub.
- resource limits, scoring, backpressure and metrics.

### Блок 4: product-level composition

- Develop `p2p_node` and focused friend plugins, not one superplugin.
- Friend plugins may own diagnostics, discovery policy, relay service config,
  pubsub gateway and the interop harness.
- `p2p_node` remains the central owner of node lifecycle.

AutoNAT, AutoRelay, DHT and pubsub algorithms must live in `fcl_p2p`.
`fcl::plugins::p2p_node` configures and runs the shared node, then exposes the
network capabilities through narrow application APIs. If a network behavior is
not implemented yet, expose a typed unsupported/limited result instead of hiding
the gap above the network layer.

## Donor Test Adoption

libp2p specs are the contract for wire behavior. go-libp2p and rust-libp2p
tests are donor criteria, not optional inspiration. FCL should not copy Go/Rust
runtime code, but it must adopt their golden vectors, scenarios, failure cases
and acceptance criteria.

For each supported libp2p protocol, create a traceability matrix:

| Protocol | Spec source | Donor tests inspected | FCL unit tests | FCL interop tests | Unsupported gaps |
| --- | --- | --- | --- | --- | --- |
| Multiformats / Peer ID | `libp2p-specs` | go-libp2p/rust-libp2p identity tests | golden byte vectors | not required | listed explicitly |
| QUIC + Ping | `libp2p-specs/quic`, `libp2p-specs/ping` | go-libp2p `test-plans`, rust-libp2p interop tests | FCL transport tests | FCL <-> Go/Rust in both directions | listed explicitly |
| Identify | `libp2p-specs/identify` | go-libp2p/rust-libp2p Identify tests | FCL encode/decode and peerstore tests | FCL <-> Go/Rust Identify exchange | listed explicitly |

Test layers:

- `golden`: golden vectors for varint, multicodec, multihash, multibase, Peer
  ID, signed records and Identify messages.
- `component`: FCL-to-FCL tests for endpoint parsing, negotiation, Ping,
  Identify and peer/path store behavior.
- `interop`: FCL client/server against go-libp2p and rust-libp2p in both
  directions.
- `plugin/system`: realistic scenarios through `fcl::plugins::p2p_node` and
  small focused friend plugins, not a parallel fake test runtime.
- `performance/stability`: latency, throughput, long sessions, reconnect, many
  peers, backpressure and peerstore recovery.

If the libp2p ecosystem already has an acceptance criterion, the FCL test must
reference that criterion. A test that is only "similar to libp2p" is not enough.
A protocol cannot be marked supported until it has spec-derived, donor-derived
and required interop coverage.

## Integration Example

```cpp
auto options = fcl::p2p::node::options{
   .certificate_pem = certificate_pem,
   .private_key_pem = private_key_pem,
};

auto node = fcl::p2p::node{runtime, options};
node.register_protocol_handler(
   fcl::p2p::protocol_id{.value = "/example/1"},
   [](fcl::p2p::node::incoming_protocol_stream incoming) -> boost::asio::awaitable<void> {
      std::vector<std::uint8_t> frame = co_await incoming.stream.async_read_frame();
      co_await incoming.stream.async_write_frame(frame);
   });

boost::asio::awaitable<void> connect_example(fcl::p2p::node& node) {
   co_await node.async_listen(fcl::quic::parse_endpoint("127.0.0.1:9443"));
   fcl::p2p::node::session_info session = co_await node.async_connect(remote_endpoint, {.expected_peer = remote_peer});
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
