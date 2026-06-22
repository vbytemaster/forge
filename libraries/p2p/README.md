# fcl_p2p

`fcl_p2p` is the peer-to-peer layer above transport sessions: peer identities,
sessions, protocol stream negotiation, peer exchange, relay reservations,
reachability probes, hole punching, path scoring, discovery protocol machinery
and GossipSub/pubsub.

## When To Use

- Nodes need to connect by peer identity, not just host/port.
- Application protocols need named streams such as `/example/1`.
- Direct transports should be tried first, with explicit relay/hole-punch
  fallback.
- Application/plugin composition needs a shared P2P transport owner; use
  `fcl::plugins::p2p::node` as the lifecycle/config/route facade above this
  low-level engine.

## When Not To Use

- Do not put application message semantics or storage semantics here.
- Do not treat P2P as authorization. Peer identity is transport identity;
  application authority is owned by consumers.
- Do not put application receipt, durable queue, storage or authorization semantics
  into peer networking. DHT, rendezvous, AutoRelay and GossipSub mechanics
  belong in `fcl_p2p`; application protocols decide what an operation means.

## Public Modules

- `fcl.p2p.identity`, `fcl.p2p.endpoint`, `fcl.p2p.node`.
- `fcl.p2p.protocol`, `fcl.p2p.message`, `fcl.p2p.negotiation`.
- `fcl.p2p.peer_store`, `fcl.p2p.discovery`, `fcl.p2p.dht`,
  `fcl.p2p.rendezvous`.
- `fcl.p2p.pubsub`.
- `fcl.p2p.relay`, `fcl.p2p.scoring`.
- `fcl.p2p.exceptions`, `fcl.p2p` aggregate.

Target: `fcl_p2p`.

Dependencies: `fcl_api`, `fcl_asio`, `fcl_transport`, `fcl_tcp`, `fcl_quic`,
`fcl_yamux`, `fcl_multiformats`, Boost.Asio and RocksDB.

Foundation compatibility modules below P2P live in `fcl_multiformats`:
`fcl.multiformats.varint`, `fcl.multiformats.multicodec`,
`fcl.multiformats.multihash`, `fcl.multiformats.multibase` and
first-class multiaddr/address support.

## Production Network Direction

`fcl_p2p` is the owner for production peer-network mechanics. The direction is
a clean C++23 libp2p-compatible implementation: FCL public types stay
FCL/Boost-style, while supported libp2p protocols must be wire-compatible with
go-libp2p and rust-libp2p.

Compatibility is not a direct libp2p dependency and not a Go/Rust runtime clone.
It means the same peer identity model, address encoding, protocol negotiation,
handshake, protocol IDs and message rules for protocols FCL marks as supported.

The canonical block order and donor test rules live in
[`docs/network/quic-p2p.md`](../../docs/network/quic-p2p.md). Keep this README
as a library overview; do not duplicate the block sequence here.

Current direction: P2P sits above first-class multiaddr, reusable
`fcl_transport`, and reusable TCP/STCP/Yamux/QUIC layers. QUIC and
TCP+TLS/Noise+Yamux direct paths are wired through private direct profiles.
Future transports must plug into the same multiaddr and transport session
boundary, not fork P2P core.

`fcl_transport` is the stream/session substrate for `fcl_p2p`; it is not an API
or RPC layer. API-over-transport lives in `fcl.transport.api`, where QUIC/P2P
bindings share frame serve-loop logic without putting `fcl::api` into
`fcl_transport`.

Network-level behaviors that must not be pushed into plugins:

- relay-only/no-direct path support;
- independent maintenance scheduling for peer exchange, reachability, relay
  reservation renewal and discovery;
- peer discovery and relay discovery;
- protocol capability negotiation;
- network limits, backpressure, metrics and shutdown behavior.

`fcl_p2p` remains free of application plugins, storage and authorization
policy. Application protocols own idempotency, acknowledgement and
permission checks above P2P.

## Examples

### Start A Node

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.p2p.identity;
import fcl.p2p.endpoint;
import fcl.p2p.node;

boost::asio::awaitable<void> start_node(fcl::asio::runtime& runtime) {
   auto options = fcl::p2p::node::options{
      .certificate_pem = certificate_pem,
      .private_key_pem = private_key_pem,
      .peer_store_path = "/var/lib/fcl/p2p/peer-store",
   };

   auto peer = fcl::p2p::make_peer_id_from_certificate_pem(certificate_pem);
   auto node = fcl::p2p::node{runtime, options};
   co_await node.async_listen(fcl::p2p::parse_endpoint("/ip4/127.0.0.1/udp/9443/quic-v1"));
   advertise_peer(peer);
}
```

Production certificates must carry the signed libp2p identity extension. Peer
IDs are not derived from a bare certificate hash in production verification
paths.

### Parse A libp2p QUIC Endpoint

`fcl::p2p::endpoint` is FCL-style public vocabulary. It accepts and emits the
libp2p address text format for compatibility, but callers do not need to model
their application API around the `multiaddr` term.

```cpp
import fcl.p2p.endpoint;

auto endpoint = fcl::p2p::parse_endpoint(
   "/ip4/127.0.0.1/udp/4001/quic-v1/p2p/12D3KooW...");

co_await node.async_listen(endpoint);

co_await node.async_listen(fcl::p2p::parse_endpoint("/ip4/127.0.0.1/tcp/4001"));
std::vector<fcl::p2p::endpoint> advertised = node.local_endpoints();
```

QUIC and TCP+TLS/Noise+Yamux are currently registered direct transports. TCP
prefers libp2p TLS (`/tls/1.0.0`) and keeps Noise as fallback. `/ws` and `/wss`
multiaddrs are parseable but direct dial/listen returns typed unsupported until
a dedicated compatibility block wires a production transport. Future transports
must use the same private direct profile boundary.

`local_endpoints()` is the full canonical listen/advertise set and each endpoint
includes `/p2p/<local-peer>`. `local_endpoint()` remains a first-endpoint
compatibility convenience for older single-listen consumers.

### Peer Store Backends

Production nodes require a persistent peer store. If `node::options` does not
provide `peer_store_backend`, `peer_store_path` opens the default RocksDB
backend. The in-memory backend is only for explicit tests and local insecure
experiments.

```cpp
auto node = fcl::p2p::node{runtime, {
   .certificate_pem = certificate_pem,
   .private_key_pem = private_key_pem,
   .peer_store_path = "/var/lib/fcl/p2p/peer-store",
}};

auto test_store = fcl::p2p::peer_store{
   {.backend = fcl::p2p::peer_store::make_memory_backend()}};
```

### Register A Protocol

```cpp
#include <cstdint>
#include <vector>

node.register_protocol_handler(fcl::p2p::protocol_id{.value = "/example/1"},
                               [](fcl::p2p::node::incoming_protocol_stream incoming)
   -> boost::asio::awaitable<void> {
   std::vector<std::uint8_t> frame = co_await incoming.stream.async_read_frame();
   co_await incoming.stream.async_write_frame(frame);
});
```

### Publish Typed APIs Above P2P

Application protocols that need request/response, typed errors and idempotent
operation receipts should expose an `fcl_api` contract and mount it through the
P2P API binding or `fcl::plugins::p2p::resolver`. P2P opens the stream and
enforces peer/path policy; API dispatch owns method calls and error projection;
the application handler owns authorization and durable state.

### Typed API Protocol Binding

`fcl.p2p.api` builds P2P API bindings on top of negotiated protocol streams.
The binding path uses `multistream-select` and the same direct, hole-punch and
relay path manager as ordinary P2P protocol streams; it must not reintroduce an
FCL-only hello envelope into direct QUIC sessions. Once a protocol stream is
open, frame serving delegates to `fcl.transport.api`; P2P keeps only P2P policy:
protocol id, known-peer checks and discovery scope.

### Connect And Open A Protocol Stream

This is the low-level engine path for custom transport owners and tests.
Application plugins should use `fcl::plugins::p2p::node::api` instead of calling these
methods directly.

```cpp
boost::asio::awaitable<void> open_example_stream(fcl::p2p::node& node) {
   fcl::p2p::node::session_info session = co_await node.async_connect(remote_endpoint, {
      .expected_peer = expected_peer,
      .timeout = std::chrono::milliseconds{10'000},
   });

   fcl::p2p::stream stream = co_await node.async_open_protocol_stream(
      session.remote_peer,
      fcl::p2p::protocol_id{.value = "/example/1"});
   use_stream(std::move(stream));
}
```

### Learn Endpoints And Probe Reachability

```cpp
import fcl.p2p.peer_store;

node.peers().learn_endpoint(
   remote_peer,
   fcl::p2p::parse_endpoint("/ip4/127.0.0.1/udp/9444/quic-v1"),
   {.bits = fcl::p2p::capabilities::direct_quic | fcl::p2p::capabilities::peer_exchange});

boost::asio::awaitable<void> update_reachability(fcl::p2p::node& node) {
   fcl::p2p::reachability::state reachability = co_await node.async_probe_reachability(observer_peer);
   if (reachability == fcl::p2p::reachability::state::relay_only) {
      schedule_relay_setup(remote_peer);
   }
}
```

### Reserve Relay Explicitly

```cpp
boost::asio::awaitable<void> open_relayed_stream(fcl::p2p::node& node) {
   fcl::p2p::relay::reservation::info reservation = co_await node.async_reserve_relay(
      relay_peer,
      {.ttl = std::chrono::milliseconds{60'000}, .max_streams = 8});

   fcl::p2p::stream relayed = co_await node.async_open_protocol_stream(
      remote_peer,
      fcl::p2p::protocol_id{.value = "/example/1"},
      {.allow_relay = true, .relay_peer = reservation.relay_peer});
   use_stream(std::move(relayed));
}
```

### Stop Cleanly

```cpp
boost::asio::awaitable<void> stop_node(fcl::p2p::node& node) {
   co_await node.async_stop();
}

// From a synchronous signal path:
node.stop();
```

## Security Notes

Production options require mTLS identity with a signed libp2p certificate
extension. `allow_insecure_test_mode` exists for tests and explicit local
experiments only; in that mode the node may use the in-memory peer store when no
path/backend is provided. Peer mismatch, TLS verification failure, missing
identity extension and invalid envelopes are correctness failures.

## Risks And Anti-Patterns

- Do not treat peer identity as application authorization. It proves transport
  identity, not permission to perform application actions.
- Do not silently fall back to relay for operations that require a direct-peer
  policy. Relay use must be explicit and visible to the caller.
- Do not put durable delivery, exactly-once semantics or storage guarantees in
  `fcl_p2p`; protocols above P2P own those contracts.
- Do not implement application retry or durable delivery loops against raw
  `node` in application plugins. Use typed request/receipt APIs for synchronous
  operations and a focused higher-level service for durable asynchronous work.
- Do not define a new P2P-only API error payload. API protocols use
  `fcl::api::error_payload` in `fcl::api::frame` error responses.
- Do not let protocol handler exceptions disappear in detached tasks. Expected
  application failures should be typed exceptions and unexpected failures should
  be counted/diagnosed.
- Do not treat `.peer_policy(...)` or `.max_inflight_per_peer(...)` as cosmetic.
  Unknown peers and too many active API calls are rejected before application API
  handlers run.
- Do not make `fcl.p2p.api` responsible for peer discovery, relay or node
  lifecycle. It is only the API protocol binding artifact.
- Do not implement AutoNAT, AutoRelay, DHT, rendezvous or pubsub loops in an
  infrastructure plugin. Network mechanics belong in `fcl_p2p`; plugins only
  configure and consume them.

## Typical Mistakes

- Do not pass plaintext secrets through protocol IDs or peer metadata.
- Do not register duplicate protocol handlers; the node rejects them.
- Do not use relay fallback silently for actions that require direct peer policy.

## Tests

`test_fcl_quic_p2p` covers identity shape, codec rejection, direct protocol echo,
path manager fallback, connect/open timeouts, peer exchange, relay, reachability,
hole punching, DHT/rendezvous component behavior and production option
validation.
