# forge_tcp

`forge_tcp` is the Boost.Asio TCP implementation for the reusable `forge_transport`
stream contract. Use it when code needs a raw bidirectional TCP byte stream.

Do not use `forge_tcp` for TLS, Yamux, P2P identity, API frame dispatch or
multiaddr parsing. Those layers sit above raw TCP.

## Public Modules

- `forge.tcp.connector`
- `forge.tcp.listener`
- `forge.tcp.connection`
- `forge.tcp.options`
- `forge.tcp.exceptions`
- `forge.tcp.transport`
- `forge.tcp`

## Direct Stream Example

```cpp
import forge.tcp.connector;
import forge.tcp.listener;
import forge.transport.endpoint;

auto local = forge::transport::endpoint{
   .host_type = forge::transport::endpoint::host_kind::ip4,
   .protocol = forge::transport::endpoint::protocol_kind::tcp,
   .host = "127.0.0.1",
   .port = 0,
};

auto listener = forge::tcp::listener{executor, local};
auto connector = forge::tcp::connector{executor};
auto connection = co_await connector.async_connect(listener.local_endpoint());
co_await connection.stream.async_write(std::span<const std::uint8_t>{bytes});
```

TCP is a byte-stream transport. Use `connection.stream.async_write(...)` and
`connection.stream.async_read()` for raw bytes. Use
`connection.stream.async_write_frame(...)` and
`connection.stream.async_read_frame()` when the caller needs FORGE length-prefixed
message boundaries over the TCP stream.

## Upgrade Surface

Use `tcp::connection` when another layer needs the native socket before TCP is
converted into a generic `transport::stream`. This is the path used by
`forge_stcp` for TLS upgrade.

```cpp
import forge.tcp.connection;
import forge.tcp.connector;

auto connector = forge::tcp::connector{executor};
auto tcp = co_await connector.async_connect_connection(remote);

// Either keep using raw TCP bytes:
co_await tcp.async_write(std::span<const std::uint8_t>{bytes});

// Or hand the socket to a higher layer:
auto socket = std::move(tcp).release_socket();
```

If no upgrade is needed, call `std::move(tcp).into_transport_stream()` or use
`connector.async_connect(...)` directly.

## Registry Example

```cpp
import forge.tcp.transport;
import forge.transport.registry;

auto registry = forge::transport::registry{};
forge::tcp::register_stream(registry, executor);

auto listener = co_await registry.async_listen_stream(local);
auto outbound = co_await registry.async_connect_stream(listener.local_endpoint());
co_await outbound.stream.async_write_frame(payload);
```

## Boundaries

- Depends only on `forge_transport`, `forge_exceptions` and Boost.Asio.
- Throws typed `forge::tcp::exceptions::*` at the TCP boundary.
- `dns`, `dns4` and `dns6` are connect-only host kinds.
- Listen accepts only concrete `ip4` and `ip6` endpoints.
- TLS-over-TCP belongs to `forge_stcp`.
