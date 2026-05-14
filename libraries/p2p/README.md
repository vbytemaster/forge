# fcl_p2p

`fcl_p2p` is the peer-to-peer layer above QUIC: peer identities, sessions,
protocol stream negotiation, peer exchange, relay reservations, reachability
probes, hole punching and path scoring.

## When To Use

- Nodes need to connect by peer identity, not just host/port.
- Application protocols need named streams such as `/example/1`.
- Direct QUIC should be tried first, with explicit relay/hole-punch fallback.

## When Not To Use

- Do not put application message semantics or storage semantics here.
- Do not treat P2P as authorization. Peer identity is transport identity; product
  authority is owned by consumers.
- Do not assume a global DHT or gossip layer exists in v1.

## Public Modules

- `fcl.p2p.identity`, `fcl.p2p.options`, `fcl.p2p.node`.
- `fcl.p2p.session`, `fcl.p2p.protocol`, `fcl.p2p.message`, `fcl.p2p.codec`.
- `fcl.p2p.peer_store`, `fcl.p2p.relay`, `fcl.p2p.scoring`, `fcl.p2p.metrics`.
- `fcl.p2p.errors`, `fcl.p2p` aggregate.

Target: `fcl_p2p`.

Dependencies: `fcl_asio`, `fcl_quic`, Boost.Asio.

## Examples

### Start A Node

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.p2p.identity;
import fcl.p2p.node;
import fcl.quic.endpoint;

boost::asio::awaitable<void> start_node(fcl::asio::runtime& runtime) {
   auto options = fcl::p2p::node_options{
      .certificate_pem = certificate_pem,
      .private_key_pem = private_key_pem,
   };

   auto peer = fcl::p2p::make_peer_id_from_certificate_pem(certificate_pem);
   auto node = fcl::p2p::node{runtime, options};
   co_await node.async_listen(fcl::quic::parse_endpoint("127.0.0.1:9443"));
   advertise_peer(peer);
}
```

### Register A Protocol

```cpp
#include <cstdint>
#include <vector>

node.register_protocol_handler(fcl::p2p::protocol_id{.value = "/example/1"}, [](fcl::p2p::incoming_protocol_stream incoming)
   -> boost::asio::awaitable<void> {
   std::vector<std::uint8_t> frame = co_await incoming.stream.async_read_frame();
   co_await incoming.stream.async_write_frame(frame);
});
```

### Connect And Open A Protocol Stream

```cpp
boost::asio::awaitable<void> open_example_stream(fcl::p2p::node& node) {
   fcl::p2p::session_info session = co_await node.async_connect(remote_endpoint, {
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
   fcl::p2p::relay_reservation_info reservation = co_await node.async_reserve_relay(
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

## Typical Mistakes

- Do not pass plaintext secrets through protocol IDs or peer metadata.
- Do not register duplicate protocol handlers; the node rejects them.
- Do not use relay fallback silently for actions that require direct peer policy.

## Tests

`test_fcl_quic_p2p` covers identity shape, codec rejection, direct protocol echo,
path manager fallback, connect/open timeouts, peer exchange, relay, reachability,
hole punching and production option validation.
