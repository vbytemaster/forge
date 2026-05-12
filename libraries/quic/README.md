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
import fcl.quic.security;

auto connector = fcl::quic::connector{runtime};
auto options = fcl::quic::client_options{
   .certificate_pem = client_certificate_pem,
   .private_key_pem = client_private_key_pem,
};
options.security.expected_sha256_fingerprint = expected_server_fingerprint;

auto connection = co_await connector.async_connect(endpoint, options);
```

### Accept Connections

```cpp
import fcl.quic.listener;

auto server_options = fcl::quic::server_options{
   .certificate_pem = server_certificate_pem,
   .private_key_pem = server_private_key_pem,
};

auto listener = fcl::quic::listener{
   runtime,
   fcl::quic::parse_endpoint("127.0.0.1:9443"),
   server_options,
};

auto inbound = co_await listener.async_accept();
```

### Open A Framed Stream

```cpp
import fcl.quic.framed_stream;

auto stream = co_await connection.async_open_stream();
auto framed = fcl::quic::framed_stream{std::move(stream), {.max_frame_size = 1 << 20}};
co_await framed.async_write_frame(payload);
```

### Decode Frames Without A Connection

```cpp
import fcl.quic.framed_stream;

auto encoded = fcl::quic::encode_frame(payload);
auto decoded = fcl::quic::decode_frame(encoded);
if (decoded.status == fcl::quic::frame_decode_status::complete) {
   consume(decoded.payload);
}
```

### Verify A Certificate Fingerprint

```cpp
import fcl.quic.security;

auto fingerprint = fcl::quic::certificate_sha256_fingerprint_from_pem(certificate_pem);
auto normalized = fcl::quic::normalize_sha256_fingerprint(fingerprint);
```

## Backpressure And Failure Model

Transport limits cover stream count, queued bytes and inbound packet queue size.
Timeouts are scoped to handshake/connect/read/write phases so callers can return
typed failures instead of vague network errors.

## Security Notes

OpenSSL 3 is the supported TLS backend. Fingerprint and mTLS failures are
correctness failures, not warnings. Test certificates must not become product
defaults.

## Typical Mistakes

- Do not put peer discovery or relay fallback in `fcl_quic`; use `fcl_p2p`.
- Do not use insecure test settings as product defaults; identity and ALPN
  checks are part of correctness.
- Do not bypass `transport_limits` for "temporary" large frames without adding a
  backpressure test.

## Tests

`test_fcl_quic_p2p` covers endpoint parsing, frame codec, loopback handshakes,
parallel streams, loss/delay/reorder fault proxy, mTLS, pinned fingerprints and
backpressure limits.
