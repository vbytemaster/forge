# fcl_quic

`fcl_quic` owns the QUIC transport layer over ngtcp2, OpenSSL 3.0+ and Boost.Asio.
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
- `fcl.quic.runtime`, `fcl.quic.metrics`, `fcl.quic.exceptions`.
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
#include <boost/asio/awaitable.hpp>

import fcl.quic.connector;
import fcl.quic.options;
import fcl.quic.security;

boost::asio::awaitable<void> connect_with_pin(
   fcl::quic::connector& connector,
   fcl::quic::endpoint endpoint) {
   auto options = fcl::quic::client_options{
      .certificate_pem = client_certificate_pem,
      .private_key_pem = client_private_key_pem,
   };
   options.security.expected_sha256_fingerprint = expected_server_fingerprint;

   fcl::quic::connection connection = co_await connector.async_connect(endpoint, options);
   use_connection(std::move(connection));
}
```

For CA-based verification, trust is explicit and host-bound:

```cpp
boost::asio::awaitable<void> connect_with_ca(
   fcl::quic::connector& connector,
   fcl::quic::endpoint endpoint) {
   auto options = fcl::quic::client_options{};
   options.security = fcl::quic::security_options{
      .verify_peer = true,
      .trusted_ca_pem = trusted_ca_bundle_pem,
   };

   // The certificate must be valid for endpoint.host through DNS/IP SAN matching.
   fcl::quic::connection connection = co_await connector.async_connect(endpoint, options);
   use_connection(std::move(connection));
}
```

### Accept Connections

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.quic.listener;

boost::asio::awaitable<void> accept_one(fcl::asio::runtime& runtime) {
   auto server_options = fcl::quic::server_options{
      .certificate_pem = server_certificate_pem,
      .private_key_pem = server_private_key_pem,
   };

   auto listener = fcl::quic::listener{
      runtime,
      fcl::quic::parse_endpoint("127.0.0.1:9443"),
      server_options,
   };

   fcl::quic::connection inbound = co_await listener.async_accept();
   handle_inbound(std::move(inbound));
}
```

### Open A Framed Stream

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.quic.framed_stream;

boost::asio::awaitable<void> write_payload(fcl::quic::connection& connection) {
   fcl::quic::stream stream = co_await connection.async_open_stream();
   auto framed = fcl::quic::framed_stream{std::move(stream), {.max_frame_size = 1 << 20}};
   co_await framed.async_write_frame(payload);
}
```

### Bind API Frames To QUIC Streams

`fcl.quic.api` is the API-over-QUIC binding. It keeps QUIC transport policy in
`fcl_quic` and contract/error semantics in `fcl_api`. The binding reads a
continuous sequence of API frames from the framed stream until the stream closes.
Codec, max concurrent API calls and deadline are enforced by the API runtime.

```cpp
import fcl.api;
import fcl.quic.api;

auto plan = fcl::api::binding()
   .serve(app.apis())
   .export_api<cache>({.id = {"cache"}, .major = 1, .min_revision = 8})
   .build();

auto binding = fcl::quic::api()
   .use(plan)
   .codec({"fcl.raw"})
   .stream_policy(fcl::quic::api_stream_policy::one_stream_per_call)
   .max_concurrent_calls(256)
   .deadline(std::chrono::seconds{5})
   .build();

boost::asio::awaitable<void> serve_api_stream(fcl::quic::connection& connection) {
   auto stream = co_await connection.async_accept_stream();
   co_await binding.accept(fcl::quic::framed_stream{std::move(stream)});
}
```

`stream_policy(...)` describes how API calls are mapped to QUIC stream usage.
`one_stream_per_call` is the simple production default. `multiplexed` keeps the
same frame semantics but allows multiple active call ids on the same framed
stream, bounded by `max_concurrent_calls(...)`.

`fcl.quic.api` does not own certificates, ALPN, listener/connector setup or
packet-level limits. Those remain in `fcl_quic` transport options.

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

OpenSSL 3.0+ is the supported TLS backend. Fingerprint and mTLS failures are
correctness failures, not warnings. CA-based client verification binds the peer
certificate to the requested endpoint host; SNI alone is not treated as identity
verification. Pinned fingerprints and custom verifiers are explicit trust paths;
they do not implicitly opt into CA hostname checks. Test certificates must not
become product defaults.

## Risks And Anti-Patterns

- Do not disable peer verification to work around certificate issues. Fix trust
  material or use an explicit pinned/custom verifier path.
- Do not confuse SNI with identity verification. CA-based verification must bind
  the certificate to the requested endpoint host.
- Do not raise frame/queue limits without backpressure tests. Oversized frames
  are a memory pressure and denial-of-service vector.
- Do not define product API envelopes in QUIC handlers. Use `fcl.quic.api` and
  `fcl::api::frame` for typed API calls over QUIC streams.
- Do not swallow handler exceptions in detached stream tasks; convert expected
  failures into typed `fcl_exception` values or API error frames.
- Do not treat `.deadline(...)` or `.max_concurrent_calls(...)` as documentation
  only; API frames are checked by the call runtime before product code runs.
- Do not put ALPN, certificate or listener lifecycle options into
  `fcl.quic.api`; those belong to the transport owner.

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
