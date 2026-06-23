# forge_api

`forge_api` is the neutral typed contract layer for local plugin-to-plugin calls
and API-over-transport bindings. It does not own HTTP, WebSocket, QUIC, P2P or
application lifecycle. It owns the contract vocabulary: API identity/version,
method descriptors, typed handles, registry/view/installer, canonical
message-oriented frames and the shared external error payload.

## When To Use

- A plugin or application needs to publish a typed C++ capability to consumers.
- A transport binding needs one contract shape for local and remote calls.
- A protocol needs stable API identity, major version and minimum revision
checks before invoking application handler code.
- Network errors must preserve typed exception identity without leaking internal
diagnostic context.

## When Not To Use

- Do not use `forge_api` as a replacement for `forge_http`, `forge_quic`,
  `forge_websocket` or `forge_p2p`.
- Do not put transport paths, peer policies, HTTP status routing or server
  lifecycle in the core API layer.
- Do not invent per-transport error DTOs. Use `forge::api::error_payload`.

## Public Modules

- `forge.api.types` — API ids, versions, refs, codec ids, call ids, method kinds,
  frame kinds, `frame` and `error_payload`.
- `forge.api.descriptor` — contract and method descriptors.
- `forge.api.error_projection` — error payload projection and remote typed-error restore.
- `forge.api.handle` — typed local/remote handle wrapper.
- `forge.api.registry` — registry, installer, view and local frame dispatch.
- `forge.api.binding` — binding plan, call runtime and protocol-neutral
  interceptors.
- `forge.api.dispatcher` — shared API frame dispatcher for stream-oriented
  bindings.
- `forge.api.exceptions` — core typed exceptions such as `method_not_found`,
  `incompatible_version` and `remote_internal`.

Target: `forge_api`.

## Local Contract

```cpp
#include <forge/api/macros.hpp>

class cache : public forge::api::contract<cache> {
 public:
   virtual ~cache() = default;

   virtual boost::asio::awaitable<models::chunk>
   read(protocol::read_chunk request) = 0;
};

FORGE_API(cache, FORGE_API_CONTRACT("cache", 1, 8), FORGE_API_METHOD(read))
```

DTO serialization stays beside the DTO owner:

```cpp
BOOST_DESCRIBE_STRUCT(protocol::read_chunk, (), (ref, offset, limit))
FORGE_DECLARE_SERIALIZATION(protocol::read_chunk)
```

For new APIs that are more naturally expressed as several C++ arguments,
`FORGE_API_METHOD(method, arg...)` records positional argument names while the
types are still deduced from the C++ method signature:

```cpp
class cache_api : public forge::api::contract<
   cache_api,
   forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~cache_api() = default;

   virtual boost::asio::awaitable<store_receipt>
   store_chunk(cache_name cache, chunk_ref ref, chunk_bytes bytes) = 0;
};

FORGE_API(
   cache_api,
   FORGE_API_CONTRACT("cache", 1, 0),
   FORGE_API_METHOD(store_chunk, cache, ref, bytes))
```

The argument names are metadata, not type declarations. Existing
`FORGE_API_METHOD(read)` one-request DTO methods keep their old source and wire
shape. Positional methods are new declarations and use an internal argument-pack
payload for frame transports. HTTP-specific request wrappers are not part of
`forge_api`; `forge_http` supports them as fields of described request DTOs and
keeps HTTP positional methods limited to path/query routing plus an optional
single JSON DTO body.

If a C++ interface has overloads or local convenience helpers with the same
method name, use the typed method macro to select the wire method explicitly:

```cpp
struct sign_request {
   std::string key_id;
   forge::crypto::sha256 digest;
};

struct sign_response {
   std::vector<std::uint8_t> signature;
};

class signature_api : public forge::api::contract<signature_api> {
 public:
   virtual ~signature_api() = default;

   virtual boost::asio::awaitable<sign_response>
   sign(sign_request request) = 0;

   boost::asio::awaitable<sign_response>
   sign(std::string key_id, forge::crypto::sha256 digest);
};

FORGE_API(signature_api,
        FORGE_API_CONTRACT("signature", 1, 0),
        FORGE_API_METHOD_TYPED(sign, sign_request, sign_response))
```

`FORGE_API_METHOD_TYPED_SINCE(...)`,
`FORGE_API_METHOD_TYPED_DEPRECATED(...)` and
`FORGE_API_METHOD_TYPED_DEPRECATED_SINCE(...)` provide the same revision and
deprecation metadata for overloaded methods.

## Publish And Consume In Process

```cpp
boost::asio::awaitable<void>
application::on_provide(forge::app::application_context& context) {
   context.apis().install<cache>(std::make_shared<cache_impl>());
   co_return;
}

boost::asio::awaitable<void>
consumer_plugin::initialize(forge::app::plugin_context& context) {
   cache_ = context.apis().get<cache>({.id = {"cache"}, .major = 1, .min_revision = 8});
   auto chunk = co_await cache_->read(protocol::read_chunk{.ref = ref});
}
```

## Message-Oriented Frame

WebSocket, QUIC, P2P and TCP-like bindings use `forge::api::frame`. HTTP does not
need to put this frame in the request body.

```cpp
auto request = forge::api::frame{
   .kind = forge::api::frame_kind::request,
   .id = {.value = 42},
   .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
   .method = "read",
   .codec = {.value = "forge.raw"},
};
forge::raw::pack(request.payload, protocol::read_chunk{.ref = ref});
```

Frame lifecycle is checked by `forge::api::call_runtime`:

- `request` opens a call id and must be unique while active.
- `response`, `error`, `cancel` and `stream_end` are terminal.
- `stream_item` keeps a streaming call active.
- unknown call ids, duplicate active ids and post-terminal frames are protocol
  errors.
- optional deadlines and max-inflight limits are enforced before dispatching the
  frame to application handler code.

Descriptor method kinds are explicit:

```cpp
return forge::api::define<cache>({.id = {"cache.events"}, .version = {1, 0}})
   .server_stream<&cache::watch, protocol::watch_chunks, models::chunk>("watch")
   .build();
```

Client and bidirectional streams use grouped frame dispatch. A client-stream
call starts with `request`, sends one or more `stream_item` frames with typed
request DTO payloads and finishes its input with `stream_end`; the server
returns a single `response` or `error`. A bidirectional stream follows the same
input shape and returns `stream_item...stream_end` or `error`.

```cpp
return forge::api::define<cache>({.id = {"cache.bulk"}, .version = {1, 0}})
   .client_stream<&cache::upload, protocol::write_chunk, protocol::write_receipt>("upload")
   .bidirectional_stream<&cache::sync, protocol::write_chunk, protocol::sync_event>("sync")
   .build();
```

## API Over Transport

`forge.transport.api` is the reusable binding for API-over-stream transports. It
sits above `forge_api` and `forge_transport`, uses `forge::transport::stream` /
`forge::transport::session`, and owns the shared frame read/write loop, codec
checks, grouped stream handling, max-inflight limits, deadlines and error
projection.

This layer must not move into `forge_transport`: transport stays a low-level
byte-stream/session contract and must not import the API contract layer.
`forge.quic.api` and `forge.p2p.api` are thin adapters or policy wrappers over the
API transport binding. WebSocket shares `forge::api::frame_dispatcher`, but not the
stream transport binding, because it is message-oriented rather than a
`transport::stream`. HTTP remains a separate binding because it is
request/response oriented rather than a long-lived bidirectional stream.

The network/P2P implementation order is tracked only in
[`docs/network/quic-p2p.md`](../../docs/network/quic-p2p.md); this README only
records the API-layer boundary.

## Interceptors

Interceptors are protocol-neutral API middleware. Use them for tracing,
authorization decisions, metrics and limits that should behave the same over
WebSocket, QUIC, P2P or an in-process test binding.

```cpp
auto plan = forge::api::binding()
   .serve(app.apis())
   .interceptor(forge::api::interceptor()
      .id("authz")
      .phase(forge::api::interceptor_phase::authorize)
      .order(10)
      .handler([](forge::api::call_context& call) -> boost::asio::awaitable<void> {
         co_await authorize_api_call(call.api, call.method, call.meta);
      })
      .build())
   .build();
```

HTTP-specific request middleware stays in `forge_http` or the `forge::plugins::http::server`
plugin facade; API interceptors do not parse HTTP headers, routes or upgrade
state.

## Error Payload

Typed FORGE exceptions are projected to one shared DTO:

```json
{
  "error": "chunk_not_found",
  "message": "chunk not found",
  "retryable": false,
  "identity": {
    "category": "cache",
    "code": 1
  }
}
```

`identity` is stable machine-readable exception identity. Internal capture
context is diagnostic-only and is not returned externally by default.

Known remote errors can be restored to typed exceptions through the same method
descriptor:

```cpp
const auto payload = forge::raw::unpack<forge::api::error_payload>(frame.payload);
const auto* method = forge::api::find_method(cache::describe(), frame.method);

try {
   forge::api::raise_remote_error(payload, method);
} catch (const cache_errors::chunk_not_found& error) {
   // Handle the same typed exception shape as local plugin calls.
}
```

Unknown remote identities become `forge::api::exceptions::remote_internal` with
the remote category/code preserved as redacted-safe context.

## Risks And Anti-Patterns

- Do not branch on exception message strings. Use typed exceptions or
  `identity.category/code`.
- Do not silently choose the first API implementation when version checks fail.
- Do not expose stack traces, secrets or capture context in network payloads.
- Do not force HTTP into a frame-only POST RPC shape; use native HTTP mapping in
  `forge.http.api.binding`.
- Do not add a builder option that only stores a flag. Any option exposed by API
  bindings must change behavior and have a test.

## Tests

`test_forge_api` covers descriptor validation, local registry/view lookup, raw
frame dispatch, shared error payload serialization, declared typed exception
projection and typed remote exception restoration.
