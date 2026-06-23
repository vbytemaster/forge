# forge_stcp

`forge_stcp` is the reusable TCP+TLS mechanics layer over `forge_tcp` and
`forge_transport`. It produces secure byte streams and deliberately does not own
P2P identity, higher-level protocol negotiation, API dispatch or multiaddr
parsing.

## Public Modules

- `forge.stcp.connection`
- `forge.stcp.connector`
- `forge.stcp.listener`
- `forge.stcp.options`
- `forge.stcp.exceptions`
- `forge.stcp`

## Direct TLS Stream

```cpp
import forge.stcp.connector;
import forge.transport.endpoint;

auto options = forge::stcp::client_options{};
options.security.trusted_ca_pem = ca_bundle_pem;
options.server_name = "service.local";
options.alpn_protocols = {"forge-api/1"};

auto connector = forge::stcp::connector{executor, options};
auto connection = co_await connector.async_connect(remote);
co_await connection.stream.async_write(payload);
```

TCP is still a byte-stream transport after TLS. Use
`connection.stream.async_write_frame(...)` and
`connection.stream.async_read_frame()` when the caller needs message boundaries.

## Upgrade Existing TCP

```cpp
import forge.stcp.connection;
import forge.tcp.connector;

auto tcp = co_await tcp_connector.async_connect_connection(remote);

// A higher layer may exchange a cleartext prelude before selecting TLS.
auto tls = co_await forge::stcp::async_upgrade_client(std::move(tcp), tls_options);
auto stream = std::move(tls).into_transport_stream();
```

## Boundaries

- Depends on `forge_tcp`, `forge_transport`, `forge_crypto`, `forge_exceptions`,
  Boost.Asio SSL and OpenSSL.
- Throws typed `forge::stcp::exceptions::*`.
- Owns TLS mechanics: certificate/key loading, trust anchors, fingerprint
  checks, optional verifier callbacks, mTLS and ALPN.
- Does not own P2P identity, security protocol selection, higher-level negotiation,
  Yamux, API frame dispatch or multiaddr mapping.
