# QUIC + P2P

`fcl_quic`, `fcl_transport` and `fcl_p2p` form the FCL peer data-plane
foundation. QUIC is one concrete transport. `fcl_transport` is the reusable
stream/session substrate. P2P is the peer/session/protocol-stream layer above
transport-shaped connections.

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
  -> fcl_multiformats
      -> multiaddr
  -> fcl_transport
      -> endpoint/stream/session/frame contracts
  -> fcl_tcp / fcl_stcp / fcl_yamux / fcl_quic
      -> concrete transport implementations and adapters
  -> fcl_p2p
      -> peer identity
      -> security upgrade, mux selection and protocol negotiation
      -> protocol streams
      -> peer store/path manager/relay
```

Concrete network libraries know sockets, TLS, QUIC engines and muxing mechanics.
`fcl_transport` exposes the common byte-stream/session contracts. P2P knows
peers, libp2p security semantics and protocol streams. Application protocols
live above P2P and define their own messages, durability and authorization.

## QUIC Responsibilities

- UDP socket/timer integration with Asio.
- ngtcp2 QUIC engine and OpenSSL 3.0+ TLS backend.
- ALPN, certificate verification, pinned fingerprints and mTLS checks.
- Framed stream codec and transport limits.
- Backpressure for streams, queued bytes and inbound datagram queues.

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
authorization. DHT/rendezvous discovery belongs in `fcl_p2p`; product plugins
must not replace it with parallel discovery loops.

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

This does not mean copying Go/Rust runtime architecture or leaking their public
API shape into FCL. FCL public APIs should keep C++/Boost-style network terms
such as `endpoint`, `resolver`, `listener`, `connector`, `session`, `stream`
and `protocol_id`. `multiaddr` is still a first-class FCL multiformats concept
because it is the libp2p address contract; `p2p::endpoint` is a typed P2P view
over a multiaddr, not a parallel source of truth.

Production network mechanics belong in `fcl_p2p`, not in plugin-local
workarounds: identity, keys, endpoint/address encoding, protocol negotiation,
Identify, Ping, peer/path store, relay, AutoNAT, DHT and pubsub. The
`fcl::plugins::p2p_node` plugin only maps config into the node, owns application
lifecycle, mounts route/API contributions and exposes safe application APIs.
Product extensions must not build parallel network-discovery, relay or gossip loops.

Ed25519, Secp256k1, ECDSA and RSA are all mandatory compatibility key families.
RSA is required for IPFS/mainline DHT compatibility. Secp256k1 and ECDSA are
required for blockchain-like networks built on top of FCL and plugins.

`fcl.crypto.base58` must be cleaned up before multiformats code depends on it:
new APIs use `std::span<const std::uint8_t>` and `std::vector<std::uint8_t>`,
while old `char` / `std::vector<char>` overloads remain compatibility wrappers.
Multiformats code should use byte-native APIs without scattered casts.

## Implementation Blocks

This section is the canonical roadmap for the network/P2P blocks. Library
READMEs may link here, but must not define a second block order.

### Block A: Roadmap Rebase + Checkpoint

- The work order changes: transport substrate work goes before continuing P2P
  protocol expansion.
- The previous conclusion "Yamux is private to P2P" is superseded. Yamux is a
  reusable muxer because it is needed by libp2p TCP, STCP/API stacks and future
  stream-session transports.
- Current order: `multiaddr -> fcl_transport -> tcp/stcp/yamux/quic -> p2p
  rebase -> p2p completion -> fcl.api.transport`.
- Existing P2P achievements remain valid checkpoints: QUIC, Ping, Identify,
  Relay v2, AutoNAT/DCUtR, DHT/Rendezvous component layer and GossipSub v1
  proof are not discarded. They must be preserved while the substrate is
  cleaned up.

### Block B: First-Class Multiaddr

- `fcl_multiformats` owns a first-class `multiaddr` concept: `component`,
  `protocol_code`, `encapsulate`, `decapsulate`, text roundtrip and binary
  roundtrip.
- Supported address components for this block: `ip4`, `ip6`, `dns`, `dns4`,
  `dns6`, `tcp`, `udp/quic-v1`, `ws`, `wss`, `p2p` and `p2p-circuit`.
- `/ws` and `/wss` are parse/store only. Dial/listen returns typed unsupported
  until a real browser/proxy requirement exists.
- `p2p::endpoint` becomes a typed view over `multiaddr`, not the source of
  truth for address encoding.

### Block C: `fcl_transport` Foundation

- `fcl_transport` owns only low-level reusable contracts: `stream`, `session`,
  `frame`, `limits` and `exceptions`.
- Add Asio-style `stream_connector`, `stream_listener`, `session_connector`,
  `session_listener` and transport registry primitives.
- `fcl_transport` must not import or model `fcl_api`, `fcl_p2p`, concrete
  QUIC/TCP types, TLS policy, Yamux policy, Peer ID, Relay, DHT, Rendezvous or
  GossipSub.
- Builders are allowed only as composition helpers over real owner-shaped
  connector/listener/session implementations. They must not hide the
  implementation or expose decorative options.

### Block D: Reusable Network Layers

- D.1 `fcl_tcp`: Boost.Asio TCP adapted to `transport::stream`.
- D.2 TCP upgrade surface: `tcp::connection` owns the native socket until an
  upper layer either turns it into `transport::stream` or releases it for a
  TLS/security upgrade.
- D.3 `fcl_stcp`: TCP+TLS mechanics adapted to secure `transport::stream`.
  This layer owns certificates, trust stores, fingerprint checks, ALPN and TLS
  handshakes, but not P2P Peer ID verification, libp2p protocol choice or
  multistream decisions.
- D.4 `fcl_yamux`: reusable muxer from `transport::stream` to
  `transport::session`, donor-derived from go-libp2p and rust-libp2p Yamux.
- D.5 `fcl_quic`: QUIC adapted to native `transport::session`.
- Current checkpoint: `fcl_quic` already has `quic::as_transport_stream(...)`
  and `quic::as_transport_session(...)`; native
  `transport::session_connector/session_listener` integration is reserved for
  the QUIC alignment pass after Yamux.
- WebSocket transport is not implemented in this block. Product
  `fcl_websocket` remains an application WebSocket API, not a libp2p transport
  claim.

### Block E: P2P Rebase

- `fcl_p2p` uses first-class multiaddr, the transport registry and reusable
  network layers.
- QUIC path: `/udp/.../quic-v1 -> fcl_quic -> transport::session`.
- TCP path: `/tcp/... -> fcl_tcp -> libp2p security upgrade -> fcl_yamux ->
  transport::session`.
- P2P owns Peer ID, Identify, libp2p Noise/TLS payload semantics,
  multistream-select, Relay, DCUtR, DHT, Rendezvous and GossipSub.
- P2P does not own generic TCP, STCP or Yamux runtime.

### Block F: P2P Completion

- Finish DHT/Rendezvous hardening, global AutoRelay discovery, TCP live interop
  and donor-doc cleanup.
- Do not claim WebSocket transport support.
- Supported P2P behavior requires spec-derived tests, donor-derived tests and
  live FCL <-> Go/Rust artifacts.
- `p2p_node` and focused friend plugins come after core behavior is proven.
  Plugins configure and expose the shared node; they do not implement network
  algorithms.

### Block G: Future API Over Transport

- Add `fcl.api.transport` only after the transport/P2P core stabilizes.
- Reuse `transport::stream` and `transport::session` for API frame serving.
- This layer owns frame read/write, codec checks, grouped streams, max inflight,
  deadlines and shared error projection.
- `fcl.quic.api`, `fcl.p2p.api` and future `stcp.api` become thin wrappers or
  policy adapters over `fcl.api.transport`.
- HTTP remains a separate request/response binding.

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

Use go-libp2p, rust-libp2p and libp2p specs as donor architecture and test
criteria. Copy layering and compatibility expectations, not Go/Rust public API
shape. Builder style is allowed only over normal owner-shaped implementations,
never as a hidden implementation or decorative options bag.

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
   co_await node.async_listen(fcl::p2p::parse_endpoint("/ip4/127.0.0.1/udp/9443/quic-v1"));
   fcl::p2p::node::session_info session = co_await node.async_connect(remote_endpoint, {.expected_peer = remote_peer});
   fcl::p2p::stream stream = co_await node.async_open_protocol_stream(
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
- Kademlia DHT and rendezvous as discovery donors; component proof and live
  FCL <-> go-libp2p/rust-libp2p artifacts are tracked in
  `docs/donors/fcl-p2p-dht-rendezvous-v1.md`.
- GossipSub v1.1 with v1.0 fallback as a pubsub/gossip donor; proof and
  hardening artifacts are tracked in `docs/donors/fcl-p2p-gossipsub-v1.md`.
- Transport abstraction before further discovery/path hardening, so DHT,
  rendezvous, Identify, AutoRelay and path scoring are not built twice around
  QUIC-only endpoint state.
- `fcl_transport` as a reusable byte-stream/session substrate, not an API/RPC
  framework. A future `fcl.api.transport` layer should sit above it and stop
  QUIC/P2P/TCP API bindings from duplicating API frame serve-loop logic.
- TCP + Noise/TLS + Yamux as the next libp2p-compatible direct transport after
  the abstraction pass.
- FCL multi-hop relay only as a future extension above the compatible one-hop
  Relay v2 baseline, never as a replacement for libp2p Relay v2 semantics.
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
