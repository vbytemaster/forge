# fcl_tcp

`fcl_tcp` is the Boost.Asio TCP implementation for the reusable `fcl_transport`
stream contract. Use it when code needs a raw bidirectional TCP byte stream.

Do not use `fcl_tcp` for TLS, Yamux, P2P identity, API frame dispatch or
multiaddr parsing. Those layers sit above raw TCP.

## Public Modules

- `fcl.tcp.connector`
- `fcl.tcp.listener`
- `fcl.tcp.options`
- `fcl.tcp.exceptions`
- `fcl.tcp.transport`
- `fcl.tcp`

## Direct Stream Example

```cpp
import fcl.tcp.connector;
import fcl.tcp.listener;
import fcl.transport.endpoint;

auto local = fcl::transport::endpoint{
   .host_type = fcl::transport::endpoint::host_kind::ip4,
   .protocol = fcl::transport::endpoint::protocol_kind::tcp,
   .host = "127.0.0.1",
   .port = 0,
};

auto listener = fcl::tcp::listener{executor, local};
auto connector = fcl::tcp::connector{executor};
auto connection = co_await connector.async_connect(listener.local_endpoint());
co_await connection.stream.async_write(std::span<const std::uint8_t>{bytes});
```

TCP is a byte-stream transport. Use `connection.stream.async_write(...)` and
`connection.stream.async_read()` for raw bytes. Use
`connection.stream.async_write_frame(...)` and
`connection.stream.async_read_frame()` when the caller needs FCL length-prefixed
message boundaries over the TCP stream.

## Registry Example

```cpp
import fcl.tcp.transport;
import fcl.transport.registry;

auto registry = fcl::transport::registry{};
fcl::tcp::register_stream(registry, executor);

auto listener = co_await registry.async_listen_stream(local);
auto outbound = co_await registry.async_connect_stream(listener.local_endpoint());
co_await outbound.stream.async_write_frame(payload);
```

## Boundaries

- Depends only on `fcl_transport`, `fcl_exception` and Boost.Asio.
- Throws typed `fcl::tcp::exceptions::*` at the TCP boundary.
- `dns`, `dns4` and `dns6` are connect-only host kinds.
- Listen accepts only concrete `ip4` and `ip6` endpoints.
- TLS-over-TCP belongs to a later `fcl_stcp` layer.
