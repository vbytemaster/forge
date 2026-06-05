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
host facade above `fcl_p2p`. It applies config, starts the node and mounts
protocol/API contributions. G.2 narrows the API around typed remote access and
local network information. Durable queues, application fan-out and read-only
diagnostics move to focused plugins or product layers. Product plugins should
not create a second node or run ad hoc retry loops against raw `fcl::p2p::node`.

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
- `/ws` and `/wss` are parse/store only. Dial/listen rejects them with a typed
  P2P error until a real browser/proxy requirement exists.
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
- Current checkpoint: `fcl_quic` exposes `quic::as_transport_stream(...)`,
  `quic::as_transport_session(...)`, native
  `transport::session_connector/session_listener` construction and
  `transport::registry` registration for `/quic-v1` endpoints.
- WebSocket transport is not implemented in this block. Product
  `fcl_websocket` remains an application WebSocket API, not a libp2p transport
  claim.

### Block E: P2P Rebase

- `fcl_p2p` uses first-class multiaddr, the transport registry and reusable
  network layers.
- QUIC path: `/udp/.../quic-v1 -> fcl_quic -> transport::session`.
- TCP path: `/tcp/... -> fcl_tcp -> libp2p security upgrade -> fcl_yamux ->
  transport::session`.
- E.1 checkpoint: QUIC is hidden behind a private P2P direct profile. The
  profile may inspect native QUIC certificates before erasing the connection to
  `transport::session`, because Peer ID verification is P2P semantics.
- E.1 checkpoint: relay and DCUtR use reusable `fcl_yamux`; the old private
  P2P Yamux runtime is removed.
- E.1 checkpoint: `/tcp`, `/ws` and `/wss` endpoints became parseable. E.2a
  supersedes the `/tcp` part by wiring direct TCP; `/ws` and `/wss` still return
  typed unsupported from P2P dial/listen.
- E.2a checkpoint: direct `/tcp/...` now uses the libp2p TCP stack shape:
  `fcl_tcp -> multistream-select -> Noise -> fcl_yamux -> transport::session`.
  FCL, go-libp2p and rust-libp2p live scenarios cover Ping, Identify and a
  framed echo stream in both FCL directions.
- E.2b checkpoint: direct TCP now prefers the libp2p TLS security branch:
  `fcl_tcp -> multistream-select -> /tls/1.0.0 -> fcl_stcp -> fcl_yamux ->
  transport::session`. Noise remains a supported fallback. FCL, go-libp2p and
  rust-libp2p live scenarios cover Ping, Identify and a framed echo stream for
  both TCP security branches.
- E.2b checkpoint: `/ws` and `/wss` remain parse/store only at multiaddr level.
- E.2c is the mandatory cleanup gate before continuing P2P feature work:
  tighten reusable Yamux resource semantics against libp2p donor behavior. This
  block must reject invalid window/buffer option combinations up front, because
  an implementation must not advertise an initial receive window it cannot
  buffer under honest peer traffic.
- E.2c must make stream-local resource abuse reset only the offending Yamux
  stream where the donor behavior permits it. A stream buffer overflow must not
  automatically fail the whole mux session; existing healthy streams must keep
  working after the reset. True malformed frames, protocol violations and
  session-level terminal errors still close the session with typed Yamux
  exceptions.
- E.2c should replace the hot `read_frame` erase-and-shift path with an
  offset/ring-buffer style parser before bulk data paths depend on Yamux. This
  is a performance hardening item, not a wire-format change.
- E.2c should release terminal Yamux stream resources as early as safety allows.
  Reset streams must release buffered bytes and wake waiters immediately, while
  stream map entries may remain until a safe reclamation boundary so existing
  handles can still report typed `stream_reset` instead of generic closed.
- E.2c keeps Yamux internal timeout-free by design: deadlines are caller policy
  and must be applied by P2P/API/transport users around operations. The cleanup
  must verify that this remains an explicit contract, not an accidental missing
  feature.
- E.2c does not add P2P protocols, API bindings or `/ws`/`/wss`
  dial/listen paths.
  It is a cleanup block for reusable `fcl_yamux` plus donor-derived regression
  tests, and it must remain compatible with the already proven TCP Noise/TLS
  and QUIC/P2P scenarios.
- E.2c implementation files:
  `libraries/yamux/session.cpp`,
  `libraries/yamux/include/fcl/yamux/options.cppm`,
  `tests/yamux/yamux_tests.cpp` and `docs/donors/fcl-yamux-v1.md`.
  No `libraries/p2p`, API, QUIC, TCP or STCP runtime code should change unless
  a regression test proves the reusable Yamux contract cannot be fixed in
  `fcl_yamux` alone.
- E.2c option invariants:
  `validate_options()` must reject `initial_window == 0`,
  `max_stream_window < initial_window`,
  `max_stream_buffer < initial_window`,
  `max_session_buffer < initial_window`, zero stream/backlog/buffer limits and
  wire-impossible frame sizes. The important contract is that FCL never sends a
  `WINDOW_UPDATE|SYN` receive credit larger than the amount it can accept from
  one honest peer stream before application reads.
- E.2c stream overflow behavior:
  `handle_data(...)` must treat stream-buffer and attributable session-buffer
  overflow as a stream-local reset. It should mark only that stream reset, clear
  its buffered inbound data with correct `session_buffer_` accounting, wake
  stream waiters, send `RST` for that stream and continue the session read loop.
  Local users of the reset stream observe typed
  `fcl::yamux::exceptions::stream_reset`; unrelated streams remain usable.
- E.2c session-failure behavior:
  malformed frame version/type/flags, stream zero misuse, invalid stream ID
  parity, duplicate stream IDs, oversized DATA frames and GOAWAY/error paths
  remain session-level failures with typed Yamux exceptions and GOAWAY where the
  current implementation already sends it.
- E.2c parser cleanup:
  `read_frame(...)` should stop erasing consumed bytes from the front of the
  receive buffer after every frame. Use an explicit consumed offset, compact
  only when useful, or an equivalent ring-buffer style parser. Preserve partial
  frame handling, multiple buffered frames and `max_frame_size` rejection.
- E.2c reclamation cleanup:
  extend the existing `is_reclaimable_stream_locked(...)` and
  `reclaim_closed_streams_locked()` path so terminal streams are removed before
  new stream cap checks, while reset handling eagerly clears inbound buffers,
  subtracts bytes from `session_buffer_` and wakes read/window waiters. This
  preserves typed reset delivery for existing handles and still releases memory
  and budget promptly.
- E.2c tests to add before implementation:
  `test_fcl_yamux` must fail on invalid options where
  `max_stream_buffer < initial_window` and
  `max_session_buffer < initial_window`; it must prove a stream-buffer overflow
  resets only the offending stream while a second stream still transfers bytes;
  it must prove an attributable session-buffer overflow follows the same
  stream-local policy; it must preserve malformed-frame session-failure tests;
  it must cover the new parser with partial frames, multiple buffered frames and
  trailing buffered bytes; and it must prove reclamation releases stream count
  and buffer budget before another stream depends on those resources.
- E.2c donor/doc update:
  `docs/donors/fcl-yamux-v1.md` must map the new stream-local reset behavior,
  invalid option invariants, parser buffering behavior and eager reclamation
  tests to libp2p spec/go-libp2p/rust-libp2p donor expectations. If a donor
  behavior is ambiguous, the doc must state the FCL policy and why it is
  compatible.
- E.2c static gates:

  ```sh
  rg -n "buffer\\.erase\\(buffer\\.begin\\(\\)" libraries/yamux/session.cpp
  rg -n "FCL_THROW\\(|throw std::runtime_error|exceptions::raise\\(" \
    libraries/yamux tests/yamux
  rg -n "import fcl\\.(api|p2p|quic|tcp|stcp|multiformats|websocket)|fcl::(api|p2p|quic|tcp|stcp|multiformats|websocket)" \
    libraries/yamux/include libraries/yamux --glob "*.cpp"
  ```

  All three searches should be empty after cleanup.
- E.2c validation:

  ```sh
  cmake --build build/fcl-typed-exceptions-debug -j 1 \
    --target fcl_yamux fcl_transport fcl_tcp fcl_stcp fcl_quic fcl_p2p fcl \
             test_fcl_yamux test_fcl_quic_p2p test_fcl_libp2p_interop

  ctest --test-dir build/fcl-typed-exceptions-debug --output-on-failure \
    -R "^(test_fcl_yamux|test_fcl_quic_p2p|test_fcl_libp2p_donor_matrix)$" \
    --timeout 900

  FCL_ENABLE_LIBP2P_INTEROP=1 \
  ctest --test-dir build/fcl-typed-exceptions-debug --output-on-failure \
    -R "^test_fcl_libp2p_interop$" \
    --timeout 1800

  git diff --check
  ```
- E.2d implements the transport buffer API compatibility guardrail before the
  next P2P completion work. `fcl_transport::stream` keeps
  `async_read()`/`async_read_frame()` as stable vector-returning convenience
  APIs, and adds an additive `fcl.transport.buffer` fast path for serious
  stream consumers.
- E.2d adds transport-owned `chunk`, `chunk_builder` and `buffer_pool`
  primitives. Chunks carry safe shared byte ownership, builders provide writable
  storage before commit and the pool reuses storage within explicit cached
  buffer/byte limits instead of hiding unbounded queues.
- E.2d adds chunk-oriented stream operations:
  `async_read_chunk()`, `async_read_frame_chunk()`, `async_write(chunk)` and
  `async_write_frame(chunk)`. Existing vector methods delegate through the new
  path where safe, so current P2P/API consumers do not need an immediate
  migration.
- E.2d adds frame fast-path helpers: `decode_frame_view(...)` returns a payload
  span plus consumed bytes without allocating a payload vector, and
  `stream::async_read_frame_chunk()` uses consumed-offset buffering with bounded
  compaction instead of front-erasing the receive buffer.
- E.2d integrates the fast path with TCP, STCP, QUIC and Yamux transport
  adapters. TCP/STCP read into pooled builders; QUIC/Yamux wrap or move already
  owned stream data into chunks without adding P2P/API semantics to
  `fcl_transport`.
- E.2d is not the final content data-plane implementation. It does not claim
  kernel zero-copy, file-backed chunks, `sendfile`, content addressing or
  `contentd` bulk-transfer readiness. Before `fcl.api.transport` or content
  bulk workloads ship, a separate benchmark/throughput block must audit
  remaining copies, allocation counts and large-chunk behavior.
- E.3 checkpoint: host-level multi-transport orchestration lives in private
  `fcl_p2p` host/node helpers, not in a new public multi-transport library and
  not inside the direct transport layer. A production node can listen on several
  direct transports at the same time, for example `/udp/.../quic-v1` and
  `/tcp/...`, and advertises the selected address set through Identify and peer
  exchange. DHT/rendezvous publication must use the same canonical host address
  source when their larger-network hardening resumes.
- E.3 replaces the single active direct listener with per-profile listeners,
  adds `node::local_endpoints()` and keeps `local_endpoint()` as a first-endpoint
  compatibility convenience. `async_listen(...)` is intentionally multi-call for
  supported direct endpoints.
- E.3 keeps `fcl::p2p::direct` direct-only: QUIC/TCP direct profiles, direct
  listen/connect/accept and no Identify, DHT, Relay, peer exchange, address
  advertisement or path scoring ownership. Relay/circuit paths stay above direct
  and are selected by host/node orchestration.
- E.3 path selection is transport-aware for direct candidates: QUIC and TCP
  endpoint records are filtered by support/freshness/backoff and then ordered by
  endpoint score. Backoff is per endpoint, so one bad TCP or QUIC address does
  not poison the whole peer.
- E.3 hardens address hygiene: advertised direct endpoints carry
  `/p2p/<local-peer>`, learned endpoint suffixes must match the verified remote
  peer, observed addresses remain separate from listen/advertised records, and
  duplicate canonical endpoints are collapsed before they enter protocol
  documents or peer-store records.
- E.3 also fixes peer exchange layering: FCL peer exchange is now selected with
  `multistream-select` like the other negotiated P2P protocol streams, rather
  than writing a private codec directly onto an unnegotiated session stream.
- P2P owns Peer ID, Identify, libp2p Noise/TLS payload semantics,
  multistream-select, Relay, DCUtR, DHT, Rendezvous and GossipSub.
- P2P does not own generic TCP, STCP or Yamux runtime.

### Block F: P2P Completion

- F.1 implemented checkpoint: production AutoRelay discovery lives in private
  `fcl_p2p` host/node orchestration. `node::async_refresh_relay_candidates()`
  and the background maintenance path use the same candidate collector over
  peer store, Identify/peer-exchange records, DHT-learned peers and
  Rendezvous-learned peers. The node maintains fresh outbound relay
  reservations, prunes expired records, backs off failed relay candidates and
  uses bounded refresh before relay fallback returns `relay_not_available`.
  Manual `relay_peer` remains an explicit override. Relay discovery is not owned
  by `direct`, plugins or product loops.
- F.2 implemented checkpoint: DHT/Rendezvous discovery lifecycle is hardened in
  private `fcl_p2p` algorithms, not transport/direct/plugins. `dht_query`
  performs bounded iterative Kademlia-style lookup with queried/failed sets,
  XOR-distance closer-peer merging, typed timeout/cancel behavior and stale
  routing/provider pruning through `peer_store`. `node::async_find_peer(...)`,
  `async_find_providers(...)` and `async_provide(...)` use the iterative path;
  `ADD_PROVIDER` remains libp2p-compatible fire-and-close and validates that
  the provider peer matches the authenticated stream peer. Rendezvous
  registration refresh replaces same peer/namespace records, discover uses
  cookie continuation and namespace scoping, signed PeerRecords are strictly
  validated before learning endpoints, and `node::async_refresh_discovery()`
  feeds fresh DHT/Rendezvous relay-capable peers into the existing AutoRelay
  reservation owner. Live many-peer donor topology artifacts remain limited by
  fixture support and are tracked as donor-matrix gaps, not unsupported runtime
  behavior.
- F.3 implemented checkpoint: connection manager and resource policy live in
  private `fcl_p2p` host/node orchestration around `session_state`, not in
  `direct`, transport libraries, plugins or product loops. `resource_manager`
  counts pending/established inbound/outbound session scopes and denials;
  `connection_manager` owns admission decisions, protected peer tags,
  low/high-watermark pruning, stale-safe removal and deterministic stop cleanup.
  Public operator/test control is additive: `protect_peer`, `unprotect_peer`
  and `is_peer_protected`.
- F.3 backoff is endpoint-local and policy-backed:
  `dial_backoff_base + dial_backoff_step * failures^2`, capped by
  `dial_backoff_max`. A failed TCP or QUIC endpoint must not poison other
  direct endpoints or fresh relay paths for the same peer.
- F.3 abuse accounting is peer-local. A peer that crosses the malformed-message
  threshold closes only the offending session, updates metrics/backoff and does
  not poison unrelated peers or transports. Protected peers survive pruning, but
  protection does not bypass hard admission when no unprotected session can be
  freed. Mixed QUIC/TCP networks must not become a denial-of-service amplifier.
- F.4 implemented checkpoint: donor docs and
  `tests/libp2p_interop/donor_cases.json` are the support-claim audit surface.
  Block F is closed only for behavior that maps to a libp2p spec/donor source,
  an FCL component test, the donor matrix and a live Go/Rust artifact where the
  fixture can exercise that behavior. Component-only proofs and fixture gaps stay
  explicit as `known_gap`; they are not treated as live compatibility claims.
- `/ws` and `/wss` remain multiaddr parse/store only. There is no P2P
  dial/listen path for browser/proxy transports in Block F.
- `p2p_node` and focused friend plugins come after core behavior is proven.
  Plugins configure and expose the shared node; they do not implement network
  algorithms.

### Block G: API Transport Foundation

- G.0 implemented checkpoint: live libp2p interop artifacts record scenario id,
  attempt id, fixture command, pid, selected/listen addresses, negotiated
  transport/security/muxer, exit code, timeout class and log tail where a
  fixture command is involved. Runner retry is limited to one fixture-timeout
  retry; protocol, security, identity and negotiation failures are not retried
  or hidden as flakes.
- G.1 implemented checkpoint: `fcl_api` is the transport-neutral contract layer.
  It owns descriptors, registry/view, frame vocabulary, `frame_dispatcher`,
  codec validation, grouped stream state, max-inflight/deadline checks and
  shared error projection. It must not import `fcl_transport`, QUIC, P2P, HTTP,
  WebSocket, plugins or product layers.
- G.1 implemented checkpoint: `fcl_api_transport` is the API-over-transport
  binding. It owns API frames over `transport::stream` and
  `transport::session`, a concurrent client read loop with pending call map,
  serialized writes, `serve_stream(...)`, `serve_session(...)`, bounded
  concurrency, close/cancel wakeups and typed transport API exceptions.
- `fcl.quic.api` and `fcl.p2p.api` are policy adapters over
  `fcl.api.transport`. QUIC policy stays in `fcl_quic`; P2P policy stays in
  `fcl_p2p` as protocol id, known-peer checks and discovery scope.
- `fcl.websocket.api` shares `fcl::api::frame_dispatcher`, but does not import
  `fcl.api.transport`, because WebSocket is message-oriented and not a
  `transport::stream`.
- HTTP remains a separate request/response binding.
- G.2 implemented checkpoint: `fcl::plugins::p2p_node` is a narrow host facade
  over `fcl_p2p`. It owns lifecycle, config-to-node mapping, local endpoint
  reporting, protocol/API route mounting and typed remote API access. Durable
  queues, application fan-out and raw network diagnostics are outside this host
  facade and move to focused plugins or product layers.
- G.3 planned checkpoint: `p2p_api_catalog` is a separate plugin for API
  descriptor discovery. It records which peers advertise API protocol ids,
  descriptors, versions, codecs and limits. Identify continues to advertise
  protocol ids; the catalog adds typed API metadata above P2P instead of
  expanding core Identify semantics.
- G.4 planned checkpoint: `p2p_diagnostics` is a read-only plugin for
  peer/path/session/relay/DHT/Rendezvous/pubsub/connection-manager health. It
  exposes operator visibility and test artifacts, not product authorization,
  routing policy or retry decisions.
- G.5 planned checkpoint: `p2p_pubsub` is a plugin facade over core GossipSub.
  It offers typed topic publish/subscribe, bounded handlers and topic policy for
  application plugins. It is not a durable queue and does not replace
  `fcl_p2p` GossipSub mesh/scoring/heartbeat mechanics.
- G.6 optional future checkpoint: `p2p_delivery` may become a separate durable
  async plugin if a product needs store-backed retry. It is separate from the
  host facade, does not promise exactly-once semantics and does not own product
  acknowledgement semantics.
- IPFS/Boxo content, provider, exchange, retrieval and pinning donors inform
  future product/content/storage layers. They are not `fcl_plugins` or
  `fcl_p2p` support claims.

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
  while host plugins expose typed remote APIs over the selected stream.

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
- TCP + Noise/TLS + Yamux direct path is accepted as the TCP compatibility
  baseline; proof is tracked in
  `docs/donors/fcl-p2p-tcp-noise-yamux-v1.md` and
  `docs/donors/fcl-p2p-tcp-tls-yamux-v1.md`.
- Chained relay paths are a future extension above the compatible one-hop Relay
  v2 baseline, never a replacement for libp2p Relay v2 semantics.
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

Validation note, 2026-06-02: during the P2P identity signature hardening review
checkpoint, one `ctest` run caught a transient `test_fcl_quic_p2p` segfault.
The follow-up full binary run (`test_fcl_quic_p2p`, 110 cases) and repeated
`ctest` gate passed. Treat any recurrence in CI as a Block E/F hardening flake
blocker and debug it from artifacts before broadening P2P scope.
