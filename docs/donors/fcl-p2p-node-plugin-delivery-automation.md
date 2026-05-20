# FCL P2P Production Networking Donors

## Purpose

`fcl_p2p` is the network layer that should grow into a clean C++23
libp2p-compatible implementation. Compatibility means protocol compatibility:
for each supported libp2p protocol, FCL must match the wire format, handshake,
Peer ID, negotiation and message rules used by go-libp2p and rust-libp2p.
`fcl::plugins::p2p_node` is the application-level transport owner above that
network layer. This note records donor patterns for addressing, discovery,
reachability, relay, hole punching, pubsub and delivery without making donor
repos or storage backends build dependencies.

## Accepted Patterns

- libp2p host/protocol split: one node owns peer identity and protocol handler
  registration, while application protocols mount named handlers above it.
- libp2p multiformats, Peer ID and key encoding: use the same varint,
  multicodec, multihash, multibase/base58btc/base32 and key encoding rules.
  Ed25519, Secp256k1, ECDSA and RSA are all part of the FCL compatibility
  target.
- libp2p multiaddress: expose an FCL typed endpoint/address model for direct,
  observed and relayed paths, but read/write the libp2p address format for
  compatibility.
- libp2p multistream-select: protocol negotiation must match the libp2p wire
  behavior for supported protocols.
- libp2p Identify and Ping: peers advertise identity, addresses, supported
  protocols, capabilities, versions and limits before higher-level protocols
  rely on them, and Ping becomes the first interop liveness target.
- libp2p Circuit Relay style explicit relay reservation: relay is a bounded
  resource with TTL, stream and byte limits; it is not an invisible trust
  boundary.
- libp2p DCUtR/AutoNAT/AutoRelay direction: direct path first, reachability
  probes, bounded hole-punch attempts and relay fallback selected by policy.
- libp2p Kademlia DHT and rendezvous: future peer discovery donors once address,
  identify and reachability foundations are stable.
- libp2p GossipSub: future pubsub/gossip donor for mesh-style topic broadcast,
  with validation and peer scoring.
- Syncthing/libtorrent style peer/path scoring: remember endpoint success,
  latency, backoff and failures; prefer higher-quality known candidates.
- Transactional outbox pattern: durable retry is an injected store contract.
  The infrastructure plugin can drive attempts, but product storage owns
  persistence and crash recovery.

## Rejected Patterns

- No direct libp2p dependency.
- No Go/Rust runtime architecture in public FCL APIs.
- No C-style public naming forced by libp2p terms; FCL APIs should use
  `endpoint`, `resolver`, `listener`, `connector`, `session`, `stream` and
  `protocol_id` where those concepts apply.
- No "close enough" tests that ignore libp2p specs, donor tests or interop
  criteria.
- No exactly-once guarantee in the P2P plugin.
- No storage backend inside `fcl_plugins`.
- No product authorization or business ACK semantics inside `p2p_node`.

## Donor Test Adoption

The donor repositories are acceptance sources, not decorative references:

- `libp2p-specs`: normative protocol behavior and wire shapes.
- `go-libp2p`: production behavior, interop test plans and edge cases.
- `rust-libp2p`: independent implementation, interop tests, protocol smoke tests
  and stream/transport compliance tests.

For each supported protocol, FCL must keep a traceability matrix with the spec
source, donor tests inspected, FCL unit tests, FCL interop tests and unsupported
gaps. Golden byte vectors should be copied or regenerated from specs/donor tests
where possible. Live interop tests must cover FCL <-> go-libp2p and FCL <->
rust-libp2p in both directions before the protocol is marked supported.

## FCL Mapping

- `fcl_p2p` owns peer/session/protocol-stream mechanics, typed endpoints,
  libp2p-compatible address encoding, Peer ID, key encoding, multistream-select,
  Identify/Ping, persistent peer/path store, relay reservation, reachability,
  AutoRelay, hole punching, DHT/rendezvous, pubsub/gossip, scoring and low-level
  errors.
- `fcl::plugins::p2p_node` owns application lifecycle, mounted route/API
  contributions, config-to-node mapping, delivery diagnostics and the optional
  `p2p_node::outbox_store`.
- Product protocols own idempotency, authorization and business-level
  acknowledgement. Raw `p2p::message` delivery means the frame was written to
  the selected protocol stream.
