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
import fcl.p2p.identity;
import fcl.p2p.node;
import fcl.quic.endpoint;

auto options = fcl::p2p::node_options{
   .certificate_pem = certificate_pem,
   .private_key_pem = private_key_pem,
};

auto peer = fcl::p2p::make_peer_id_from_certificate_pem(certificate_pem);
auto node = fcl::p2p::node{runtime, options};
co_await node.async_listen(fcl::quic::parse_endpoint("127.0.0.1:9443"));
```

### Register A Protocol

```cpp
node.register_protocol_handler(fcl::p2p::protocol_id{.value = "/example/1"}, [](fcl::p2p::incoming_protocol_stream incoming)
   -> boost::asio::awaitable<void> {
   auto frame = co_await incoming.stream.async_read_frame();
   co_await incoming.stream.async_write_frame(frame);
});
```

### Connect And Open A Protocol Stream

```cpp
auto session = co_await node.async_connect(remote_endpoint, {
   .expected_peer = expected_peer,
   .timeout = std::chrono::milliseconds{10'000},
});

auto stream = co_await node.async_open_protocol_stream(
   session.remote_peer,
   fcl::p2p::protocol_id{.value = "/example/1"});
```

### Learn Endpoints And Probe Reachability

```cpp
import fcl.p2p.peer_store;

node.peers().learn_endpoint(
   remote_peer,
   fcl::quic::parse_endpoint("127.0.0.1:9444"),
   {.bits = fcl::p2p::capabilities::direct_quic | fcl::p2p::capabilities::peer_exchange});

auto reachability = co_await node.async_probe_reachability(observer_peer);
if (reachability == fcl::p2p::reachability_state::relay_only) {
   schedule_relay_setup(remote_peer);
}
```

### Reserve Relay Explicitly

```cpp
auto reservation = co_await node.async_reserve_relay(
   relay_peer,
   {.ttl = std::chrono::milliseconds{60'000}, .max_streams = 8});

auto relayed = co_await node.async_open_protocol_stream(
   remote_peer,
   fcl::p2p::protocol_id{.value = "/example/1"},
   {.allow_relay = true, .relay_peer = reservation.relay_peer});
```

### Stop Cleanly

```cpp
co_await node.async_stop();
// or, from a synchronous signal path:
node.stop();
```

## Security Notes

Production options require mTLS identity. `allow_insecure_test_mode` exists for
tests and explicit local experiments only. Peer mismatch, TLS verification
failure and invalid envelopes are correctness failures.

## Typical Mistakes

- Do not pass plaintext secrets through protocol IDs or peer metadata.
- Do not register duplicate protocol handlers; the node rejects them.
- Do not use relay fallback silently for actions that require direct peer policy.

## Tests

`test_fcl_quic_p2p` covers identity shape, codec rejection, direct protocol echo,
path manager fallback, connect/open timeouts, peer exchange, relay, reachability,
hole punching and production option validation.
