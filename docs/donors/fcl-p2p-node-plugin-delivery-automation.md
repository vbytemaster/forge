# FCL P2P Production Networking Donors

## Purpose

`fcl_p2p` is the network layer that should grow into a clean C++23
libp2p-compatible implementation. Compatibility means protocol compatibility:
for each supported libp2p protocol, FCL must match the wire format, handshake,
Peer ID, negotiation and message rules used by go-libp2p and rust-libp2p.
`fcl::plugins::p2p_node` is the application-level host facade above that network
layer. This note records donor patterns for addressing, discovery, reachability,
relay, hole punching, pubsub and plugin composition without making donor repos
or storage backends build dependencies.

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
- libp2p Kademlia DHT and rendezvous: peer discovery donor proof now lives in
  `fcl-p2p-dht-rendezvous-v1.md`; plugin discovery loops must not duplicate
  that core.
- libp2p GossipSub: pubsub/gossip donor proof now lives in
  `fcl-p2p-gossipsub-v1.md`; product topic APIs should consume a focused plugin
  facade instead of raw core internals.
- Syncthing/libtorrent style peer/path scoring: remember endpoint success,
  latency, backoff and failures; prefer higher-quality known candidates.
- Kubo `CoreAPI` style facade: consumers use narrow API surfaces backed by a
  node instead of constructing or mutating the node directly.
- Rust libp2p `SwarmBuilder` style composition: transports, security upgrades,
  muxers, relay client and behaviours are composed declaratively; application
  plugins do not run ad hoc protocol loops.
- Go libp2p host services: DHT, Identify, AutoNAT, AutoRelay, relay service,
  connection limits and resource policy are host/network behaviours, not
  product plugin code.
- Boxo service decomposition: routing, provider, exchange, retrieval and
  gateway backends are focused services. FCL should copy that separation, not
  collapse content, diagnostics, delivery and P2P host ownership into one
  plugin.
- Transactional outbox pattern: durable retry can exist as an injected store
  contract in a future `p2p_delivery` plugin, but it is not part of the target
  `p2p_node` host facade.

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
- No IPFS/Boxo content, provider, exchange, retrieval, pinning or gateway
  semantics inside `fcl_p2p` or `p2p_node`; those are donors for future
  product/content/storage layers.

## Donor Test Adoption

The canonical compatibility test rules live in
[`docs/network/quic-p2p.md`](../network/quic-p2p.md). This donor note records
which external projects provide accepted patterns and criteria:

- `libp2p-specs`: normative protocol behavior and wire shapes.
- `go-libp2p`: production behavior, interop test plans and edge cases.
- `rust-libp2p`: independent implementation, interop tests, protocol smoke tests
  and stream/transport compliance tests.
- `kubo`: `CoreAPI` and plugin lifecycle donor for narrow API facades over a
  running node.
- `boxo`: service decomposition donor for routing/provider/exchange/retrieval
  boundaries above or beside a host, not inside the host facade.

## FCL Mapping

- `fcl_p2p` owns peer/session/protocol-stream mechanics, typed endpoints,
  libp2p-compatible address encoding, Peer ID, key encoding, multistream-select,
  Identify/Ping, persistent peer/path store, relay reservation, reachability,
  AutoRelay, hole punching, DHT/rendezvous, pubsub/gossip, scoring and low-level
  errors.
- `fcl::plugins::p2p_node` owns application lifecycle, mounted route/API
  contributions, config-to-node mapping, local endpoint reporting and typed
  remote API access. Durable queues, application fan-out and raw peer metrics
  are outside the target contract.
- `p2p_api_catalog`, `p2p_diagnostics`, `p2p_pubsub` and optional
  `p2p_delivery` are focused friend-plugin directions. They compose through
  `p2p_node` and `fcl_p2p` instead of reimplementing network behaviours.
- Product protocols own idempotency, authorization and business-level
  acknowledgement. Raw `p2p::message` delivery means the frame was written to
  the selected protocol stream.
