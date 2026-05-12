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
import fcl.p2p.node;
import fcl.quic.endpoint;

auto options = fcl::p2p::node_options{
   .certificate_pem = certificate_pem,
   .private_key_pem = private_key_pem,
};

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
