# fcl_p2p

`fcl_p2p` is the peer-to-peer layer above QUIC: peer identities, sessions,
protocol stream negotiation, peer exchange, relay reservations, reachability
probes, hole punching and path scoring.

## When To Use

- Nodes need to connect by peer identity, not just host/port.
- Application protocols need named streams such as `/example/1`.
- Direct QUIC should be tried first, with explicit relay/hole-punch fallback.
- Application/plugin composition needs a shared P2P transport owner; use
  `fcl::plugins::p2p_node` for retry, outbox and relay policy above this
  low-level engine.

## When Not To Use

- Do not put application message semantics or storage semantics here.
- Do not treat P2P as authorization. Peer identity is transport identity; product
  authority is owned by consumers.
- Do not assume a global DHT or gossip layer exists today. Those are future
  `fcl_p2p` network services, not plugin-level shortcuts.

## Public Modules

- `fcl.p2p.identity`, `fcl.p2p.endpoint`, `fcl.p2p.node`.
- `fcl.p2p.protocol`, `fcl.p2p.message`, `fcl.p2p.codec`.
- `fcl.p2p.peer_store`, `fcl.p2p.relay`, `fcl.p2p.scoring`.
- `fcl.p2p.errors`, `fcl.p2p` aggregate.

Target: `fcl_p2p`.

Dependencies: `fcl_api`, `fcl_asio`, `fcl_quic`, `fcl_multiformats`, Boost.Asio.

Foundation compatibility modules below P2P live in `fcl_multiformats`:
`fcl.multiformats.varint`, `fcl.multiformats.multicodec`,
`fcl.multiformats.multihash`, `fcl.multiformats.multibase` and
`fcl.multiformats.address`.

## Production Network Roadmap

`fcl_p2p` is the owner for production peer-network mechanics. The direction is
a clean C++23 libp2p-compatible implementation: FCL public types stay
FCL/Boost-style, while supported libp2p protocols must be wire-compatible with
go-libp2p and rust-libp2p.

Compatibility is not a direct libp2p dependency and not a Go/Rust runtime clone.
It means the same peer identity model, address encoding, protocol negotiation,
handshake, protocol IDs and message rules for protocols FCL marks as supported.

The canonical detailed roadmap and donor test rules live in
[`docs/network/quic-p2p.md`](../../docs/network/quic-p2p.md). Keep this README
as a library overview; do not duplicate the full block roadmap here.

Network-level behaviors that must not be pushed into plugins:

- relay-only/no-direct path support;
- independent maintenance scheduling for peer exchange, reachability, relay
  reservation renewal and discovery;
- peer discovery and relay discovery;
- protocol capability negotiation;
- network limits, backpressure, metrics and shutdown behavior.

`fcl_p2p` remains free of application plugins, product storage and product
authorization. Product protocols own idempotency, business acknowledgement and
permission checks above P2P.

## Examples

### Start A Node

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.p2p.identity;
import fcl.p2p.node;
import fcl.quic.endpoint;

boost::asio::awaitable<void> start_node(fcl::asio::runtime& runtime) {
   auto options = fcl::p2p::node::options{
      .certificate_pem = certificate_pem,
      .private_key_pem = private_key_pem,
   };

   auto peer = fcl::p2p::make_peer_id_from_certificate_pem(certificate_pem);
   auto node = fcl::p2p::node{runtime, options};
   co_await node.async_listen(fcl::quic::parse_endpoint("127.0.0.1:9443"));
   advertise_peer(peer);
}
```

### Parse A libp2p QUIC Endpoint

`fcl::p2p::endpoint` is FCL-style public vocabulary. It accepts and emits the
libp2p address text format for compatibility, but callers do not need to model
their product API around the `multiaddr` term.

```cpp
import fcl.p2p.endpoint;

auto endpoint = fcl::p2p::parse_endpoint(
   "/ip4/127.0.0.1/udp/4001/quic-v1/p2p/12D3KooW...");

co_await node.async_listen(endpoint.quic_endpoint());
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

### Send A Product Message

`fcl::p2p::message` owns protocol id, codec metadata and serialized bytes. For
raw DTOs, construct it directly from the product value; callers do not need to
manually allocate a byte buffer.

```cpp
struct announce_chunk {
   std::string ref;
};

BOOST_DESCRIBE_STRUCT(announce_chunk, (), (ref))
FCL_DECLARE_SERIALIZATION(announce_chunk)

auto message = fcl::p2p::message{
   fcl::p2p::protocol_id{.value = "/product/chunks/announce/1"},
   announce_chunk{.ref = "bafk..."}};

co_await p2p->broadcast(std::move(message));
```

### Register A Typed API Protocol

`fcl.p2p.api` builds a protocol handler artifact. The node remains a P2P
transport owner; API sessions are surfaced by the binding. The binding validates
the negotiated protocol id, optional peer policy, configured codec and per-peer
max inflight calls before product handlers run.

```cpp
import fcl.api;
import fcl.p2p.api;

auto plan = fcl::api::binding()
   .serve(app.apis())
   .export_api<peer_index>({.id = {"peer.index"}, .major = 1, .min_revision = 2})
   .require_peer_api<client_session>({.id = {"client.session"}, .major = 1})
   .build();

auto binding = fcl::p2p::api()
   .use(plan)
   .protocol_id("/fcl/api/1")
   .codec({"fcl.raw"})
   .discovery_scope({.value = "storage"})
   .max_inflight_per_peer(64)
   .build();

binding.on_session([](fcl::api::session& session) -> boost::asio::awaitable<void> {
   auto client = session.view().get<client_session>({.id = {"client.session"}, .major = 1});
   co_await client->notify(protocol::peer_ready{});
});

node.register_protocol_handler(binding.protocol(), binding.handler());
```

The binding handles a continuous framed API session over the accepted P2P
protocol stream. It does not own peer identity, relay, hole punching, peer-store
lifecycle or node bootstrap; those stay on `fcl::p2p::node`.

Use `fcl::p2p::api(node)` only when the binding must enforce node-backed peer
policy such as `require_known_peer`. For app/plugin composition, prefer a
transport-owner plugin such as `fcl::plugins::p2p_node`; product plugins publish
route/API contributions to that owner instead of registering handlers directly
on the node.

### Connect And Open A Protocol Stream

This is the low-level engine path for custom transport owners and tests. Product
plugins should use `fcl::plugins::p2p_node::api` instead of calling these
methods directly.

```cpp
boost::asio::awaitable<void> open_example_stream(fcl::p2p::node& node) {
   fcl::p2p::node::session_info session = co_await node.async_connect(remote_endpoint, {
      .expected_peer = expected_peer,
      .timeout = std::chrono::milliseconds{10'000},
   });

   fcl::quic::framed_stream stream = co_await node.async_open_protocol_stream(
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
   fcl::quic::parse_endpoint("127.0.0.1:9444"),
   {.bits = fcl::p2p::capabilities::direct_quic | fcl::p2p::capabilities::peer_exchange});

boost::asio::awaitable<void> update_reachability(fcl::p2p::node& node) {
   fcl::p2p::reachability_state reachability = co_await node.async_probe_reachability(observer_peer);
   if (reachability == fcl::p2p::reachability_state::relay_only) {
      schedule_relay_setup(remote_peer);
   }
}
```

### Reserve Relay Explicitly

```cpp
boost::asio::awaitable<void> open_relayed_stream(fcl::p2p::node& node) {
   fcl::p2p::relay_reservation::info reservation = co_await node.async_reserve_relay(
      relay_peer,
      {.ttl = std::chrono::milliseconds{60'000}, .max_streams = 8});

   fcl::quic::framed_stream relayed = co_await node.async_open_protocol_stream(
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

Production options require mTLS identity. `allow_insecure_test_mode` exists for
tests and explicit local experiments only. Peer mismatch, TLS verification
failure and invalid envelopes are correctness failures.

## Risks And Anti-Patterns

- Do not treat peer identity as product authorization. It proves transport
  identity, not permission to perform product actions.
- Do not silently fall back to relay for operations that require a direct-peer
  policy. Relay use must be explicit and visible to the caller.
- Do not put durable delivery, exactly-once semantics or storage guarantees in
  `fcl_p2p`; protocols above P2P own those contracts.
- Do not implement application retry/outbox loops against raw `node` in product
  plugins. Use `fcl::plugins::p2p_node` so one transport owner centralizes path
  policy, relay trust and delivery diagnostics.
- Do not define a new P2P-only API error payload. API protocols use
  `fcl::api::error_payload` in `fcl::api::frame` error responses.
- Do not let protocol handler exceptions disappear in detached tasks. Expected
  product failures should be typed exceptions and unexpected failures should be
  counted/diagnosed.
- Do not treat `.peer_policy(...)` or `.max_inflight_per_peer(...)` as cosmetic.
  Unknown peers and too many active API calls are rejected before product API
  handlers run.
- Do not make `fcl.p2p.api` responsible for peer discovery, relay or node
  lifecycle. It is only the API protocol binding artifact.
- Do not implement AutoNAT, AutoRelay, DHT, rendezvous or pubsub in an
  infrastructure plugin. Missing network mechanics must be added to `fcl_p2p`
  or exposed as typed unsupported behavior.

## Typical Mistakes

- Do not pass plaintext secrets through protocol IDs or peer metadata.
- Do not register duplicate protocol handlers; the node rejects them.
- Do not use relay fallback silently for actions that require direct peer policy.

## Tests

`test_fcl_quic_p2p` covers identity shape, codec rejection, direct protocol echo,
path manager fallback, connect/open timeouts, peer exchange, relay, reachability,
hole punching and production option validation.
