# FCL P2P Node Plugin Delivery Automation Donors

## Purpose

`fcl::plugins::p2p_node` is the application-level transport owner above
`fcl_p2p`. This note records the donor patterns used for retry, relay policy and
outbox delivery without making donor repos or storage backends build
dependencies.

## Accepted Patterns

- libp2p host/protocol split: one node owns peer identity and protocol handler
  registration, while application protocols mount named handlers above it.
- libp2p Circuit Relay style explicit relay reservation: relay is a bounded
  resource with TTL, stream and byte limits; it is not an invisible trust
  boundary.
- libp2p DCUtR/AutoNAT/AutoRelay direction: direct path first, reachability
  probes, bounded hole-punch attempts and relay fallback selected by policy.
- Syncthing/libtorrent style peer/path scoring: remember endpoint success,
  latency, backoff and failures; prefer higher-quality known candidates.
- Transactional outbox pattern: durable retry is an injected store contract.
  The infrastructure plugin can drive attempts, but product storage owns
  persistence and crash recovery.

## Rejected Patterns

- No direct libp2p dependency.
- No DHT or global discovery baseline in this iteration.
- No exactly-once guarantee in the P2P plugin.
- No storage backend inside `fcl_plugins`.
- No product authorization or business ACK semantics inside `p2p_node`.

## FCL Mapping

- `fcl_p2p` owns peer/session/protocol-stream mechanics, relay reservation,
  reachability, hole punching, scoring and low-level errors.
- `fcl::plugins::p2p_node` owns application lifecycle, mounted route/API
  contributions, retry policy, relay trust policy, delivery diagnostics and the
  optional `p2p_node::outbox_store`.
- Product protocols own idempotency, authorization and business-level
  acknowledgement. Raw `p2p::message` delivery means the frame was written to
  the selected protocol stream.
