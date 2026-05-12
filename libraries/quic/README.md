# fcl_quic

`fcl_quic` owns the QUIC transport layer over ngtcp2, OpenSSL 3 and Boost.Asio.
It exposes endpoints, security options, listeners, connectors, connections and
framed streams without defining application protocols.

## When To Use

- Need multiplexed, TLS-backed streams over UDP.
- Need pinned certificate fingerprints or mTLS-style checks at transport level.
- Need bounded frame sizes, connection slots and packet queues.

## When Not To Use

- Do not put peer discovery or relay policy here; that is `fcl_p2p`.
- Do not put product protocol messages here; use framed streams as substrate.
- Do not disable peer verification outside explicit tests.

## Public Modules

- `fcl.quic.endpoint`, `fcl.quic.options`, `fcl.quic.security`.
- `fcl.quic.listener`, `fcl.quic.connector`, `fcl.quic.connection`.
- `fcl.quic.stream`, `fcl.quic.framed_stream`.
- `fcl.quic.runtime`, `fcl.quic.metrics`, `fcl.quic.errors`.
- `fcl.quic` — aggregate import.

Target: `fcl_quic`.

Dependencies: `fcl_asio`, Boost.Asio, OpenSSL::SSL/Crypto, ngtcp2 and
ngtcp2 crypto OpenSSL backend.

## Examples

### Parse Endpoint

```cpp
import fcl.quic.endpoint;

auto endpoint = fcl::quic::parse_endpoint("127.0.0.1:9443");
auto authority = endpoint.authority();
```

### Connect With Expected Peer

```cpp
import fcl.quic.connector;
import fcl.quic.options;

auto connector = fcl::quic::connector{runtime};
auto connection = co_await connector.async_connect(endpoint, fcl::quic::client_options{});
```

### Open A Framed Stream

```cpp
import fcl.quic.framed_stream;

auto stream = co_await connection.async_open_stream();
auto framed = fcl::quic::framed_stream{std::move(stream), {.max_frame_size = 1 << 20}};
co_await framed.async_write_frame(payload);
```

## Backpressure And Failure Model

Transport limits cover stream count, queued bytes and inbound packet queue size.
Timeouts are scoped to handshake/connect/read/write phases so callers can return
typed failures instead of vague network errors.

## Security Notes

OpenSSL 3 is the supported TLS backend. Fingerprint and mTLS failures are
correctness failures, not warnings. Test certificates must not become product
defaults.

## Tests

`test_fcl_quic_p2p` covers endpoint parsing, frame codec, loopback handshakes,
parallel streams, loss/delay/reorder fault proxy, mTLS, pinned fingerprints and
backpressure limits.
