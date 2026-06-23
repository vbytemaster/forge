# forge_quic

`forge_quic` owns the QUIC transport layer over ngtcp2, OpenSSL 3.0+ and Boost.Asio.
It exposes endpoints, security options, listeners, connectors, connections and
framed streams without defining application protocols.

## Transport Alignment Checkpoint

`forge_quic` is a native `forge_transport` session transport. The
`forge.quic.transport` module exposes:

- `quic::as_transport_stream(...)` and `quic::as_transport_session(...)` for
  adapting existing QUIC objects.
- `quic::make_session_connector(...)` and
  `quic::make_session_listener(...)` for direct `transport::session` usage.
- `quic::register_session(...)` for `transport::registry` integration.
- `quic::to_transport_limits(...)` and `quic::from_transport_limits(...)` for
  explicit limit mapping.

QUIC is already a multiplexed session transport. TCP and STCP remain
byte-stream transports; they become sessions only after a muxer such as Yamux.

## When To Use

- Need multiplexed, TLS-backed streams over UDP.
- Need pinned certificate fingerprints or mTLS-style checks at transport level.
- Need bounded frame sizes, connection slots and packet queues.

## When Not To Use

- Do not put peer discovery or relay policy here; that is `forge_p2p`.
- Do not put application protocol messages here; use framed streams as substrate.
- Do not disable peer verification outside explicit tests.

## Public Modules

- `forge.quic.endpoint`, `forge.quic.options`, `forge.quic.security`.
- `forge.quic.listener`, `forge.quic.connector`, `forge.quic.connection`.
- `forge.quic.stream`, `forge.quic.framed_stream`.
- `forge.quic.runtime`, `forge.quic.metrics`, `forge.quic.exceptions`.

Target: `forge_quic`.

Dependencies: `forge_asio`, Boost.Asio, OpenSSL::SSL/Crypto, ngtcp2 and
ngtcp2 crypto OpenSSL backend.

## Examples

### Parse Endpoint

```cpp
import forge.quic.endpoint;

auto endpoint = forge::quic::parse_endpoint("127.0.0.1:9443");
auto authority = endpoint.authority();
```

### Connect With Expected Peer

```cpp
#include <boost/asio/awaitable.hpp>

import forge.quic.connector;
import forge.quic.options;
import forge.quic.security;

boost::asio::awaitable<void> connect_with_pin(
   forge::quic::connector& connector,
   forge::quic::endpoint endpoint) {
   auto options = forge::quic::client_options{
      .certificate_pem = client_certificate_pem,
      .private_key_pem = client_private_key_pem,
   };
   options.security.expected_sha256_fingerprint = expected_server_fingerprint;

   forge::quic::connection connection = co_await connector.async_connect(endpoint, options);
   use_connection(std::move(connection));
}
```

For CA-based verification, trust is explicit and host-bound:

```cpp
boost::asio::awaitable<void> connect_with_ca(
   forge::quic::connector& connector,
   forge::quic::endpoint endpoint) {
   auto options = forge::quic::client_options{};
   options.security = forge::quic::security_options{
      .verify_peer = true,
      .trusted_ca_pem = trusted_ca_bundle_pem,
   };

   // The certificate must be valid for endpoint.host through DNS/IP SAN matching.
   forge::quic::connection connection = co_await connector.async_connect(endpoint, options);
   use_connection(std::move(connection));
}
```

### Accept Connections

```cpp
#include <boost/asio/awaitable.hpp>

import forge.quic.listener;

boost::asio::awaitable<void> accept_one(forge::asio::runtime& runtime) {
   auto server_options = forge::quic::server_options{
      .certificate_pem = server_certificate_pem,
      .private_key_pem = server_private_key_pem,
   };

   auto listener = forge::quic::listener{
      runtime,
      forge::quic::parse_endpoint("127.0.0.1:9443"),
      server_options,
   };

   forge::quic::connection inbound = co_await listener.async_accept();
   handle_inbound(std::move(inbound));
}
```

### Open A Framed Stream

```cpp
#include <boost/asio/awaitable.hpp>

import forge.quic.framed_stream;

boost::asio::awaitable<void> write_payload(forge::quic::connection& connection) {
   forge::quic::stream stream = co_await connection.async_open_stream();
   auto framed = forge::quic::framed_stream{std::move(stream), {.max_frame_size = 1 << 20}};
   co_await framed.async_write_frame(payload);
}
```

### Bind API Frames To QUIC Streams

`forge.quic.api` is the API-over-QUIC adapter. It keeps QUIC transport policy in
`forge_quic`, contract/error semantics in `forge_api`, and delegates frame-loop
mechanics to `forge.transport.api`.

```cpp
import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.quic.api;

auto plan = forge::api::binding()
   .serve(app.apis())
   .export_api<cache>({.id = {"cache"}, .major = 1, .min_revision = 8})
   .build();

auto binding = forge::quic::api()
   .use(plan)
   .codec({"forge.raw"})
   .max_concurrent_calls(256)
   .deadline(std::chrono::seconds{5})
   .build();

boost::asio::awaitable<void> serve_api_stream(forge::quic::connection& connection) {
   auto stream = co_await connection.async_accept_stream();
   co_await binding.accept(std::move(stream));
}
```

`forge.quic.api` does not own certificates, ALPN, listener/connector setup or
packet-level limits. Those remain in `forge_quic` transport options. It also does
not own the generic API frame state machine; that lives in `forge.transport.api`.

### Decode Frames Without A Connection

```cpp
import forge.quic.framed_stream;

auto encoded = forge::quic::encode_frame(payload);
auto decoded = forge::quic::decode_frame(encoded);
if (decoded.status == forge::quic::frame_decode_status::complete) {
   consume(decoded.payload);
}
```

### Verify A Certificate Fingerprint

```cpp
import forge.quic.security;

auto fingerprint = forge::quic::certificate_sha256_fingerprint_from_pem(certificate_pem);
auto normalized = forge::quic::normalize_sha256_fingerprint(fingerprint);
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
become application defaults.

## Risks And Anti-Patterns

- Do not disable peer verification to work around certificate issues. Fix trust
  material or use an explicit pinned/custom verifier path.
- Do not confuse SNI with identity verification. CA-based verification must bind
  the certificate to the requested endpoint host.
- Do not raise frame/queue limits without backpressure tests. Oversized frames
  are a memory pressure and denial-of-service vector.
- Do not define application API envelopes in QUIC handlers. Use `forge.quic.api` and
  `forge::api::frame` for typed API calls over QUIC streams.
- Do not swallow handler exceptions in detached stream tasks; convert expected
  failures into typed `forge_exceptions` values or API error frames.
- Do not treat `.deadline(...)` or `.max_concurrent_calls(...)` as documentation
  only; API frames are checked by the call runtime before application handlers run.
- Do not put ALPN, certificate or listener lifecycle options into
  `forge.quic.api`; those belong to the transport owner.

## Typical Mistakes

- Do not put peer discovery or relay fallback in `forge_quic`; use `forge_p2p`.
- Do not use insecure test settings as application defaults; identity and ALPN
  checks are part of correctness.
- Do not bypass `transport_limits` for "temporary" large frames without adding a
  backpressure test.

## Tests

`test_forge_quic_p2p` covers endpoint parsing, frame codec, loopback handshakes,
parallel streams, loss/delay/reorder fault proxy, mTLS, pinned fingerprints and
backpressure limits.
