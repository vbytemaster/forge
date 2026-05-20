# fcl_api

`fcl_api` is the neutral typed contract layer for local plugin-to-plugin calls
and API-over-transport bindings. It does not own HTTP, WebSocket, QUIC, P2P or
application lifecycle. It owns the contract vocabulary: API identity/version,
method descriptors, typed handles, registry/view/installer, canonical
message-oriented frames and the shared external error payload.

## When To Use

- A plugin or application needs to publish a typed C++ capability to consumers.
- A transport binding needs one contract shape for local and remote calls.
- A protocol needs stable API identity, major version and minimum revision
checks before invoking product code.
- Network errors must preserve typed exception identity without leaking internal
diagnostic context.

## When Not To Use

- Do not use `fcl_api` as a replacement for `fcl_http`, `fcl_quic`,
  `fcl_websocket` or `fcl_p2p`.
- Do not put transport paths, peer policies, HTTP status routing or server
  lifecycle in the core API layer.
- Do not invent per-transport error DTOs. Use `fcl::api::error_payload`.

## Public Modules

- `fcl.api.types` — API ids, versions, refs, codec ids, call ids, method kinds,
  frame kinds, `frame` and `error_payload`.
- `fcl.api.descriptor` — contract and method descriptors.
- `fcl.api.errors` — error payload projection and remote typed-error restore.
- `fcl.api.handle` — typed local/remote handle wrapper.
- `fcl.api.registry` — registry, installer, view and local frame dispatch.
- `fcl.api.binding` — connection, session, binding plan, call runtime and
  protocol-neutral interceptors.
- `fcl.api` — aggregate import.
- `fcl.api.exceptions` — core typed exceptions such as `method_not_found`,
  `incompatible_version` and `remote_internal`.

Target: `fcl_api`.

## Local Contract

```cpp
class cache {
 public:
   virtual ~cache() = default;

   virtual boost::asio::awaitable<models::chunk>
   read(protocol::read_chunk request) = 0;

   static fcl::api::descriptor describe() {
      return fcl::api::contract<cache>({.id = {"cache"}, .version = {1, 8}})
         .method<&cache::read, protocol::read_chunk, models::chunk>("read")
         .error<cache_errors::chunk_not_found>(
            "chunk_not_found",
            {.status_code = fcl::api::status::not_found, .retryable = false})
         .build();
   }
};
```

Product DTO serialization stays beside the DTO owner:

```cpp
BOOST_DESCRIBE_STRUCT(protocol::read_chunk, (), (ref, offset, limit))
FCL_DECLARE_SERIALIZATION(protocol::read_chunk)
```

## Publish And Consume In Process

```cpp
boost::asio::awaitable<void>
application::on_provide(fcl::app::application_context& context) {
   context.apis().install<cache>(
      cache::describe(),
      std::make_shared<rocks_cache>());
   co_return;
}

boost::asio::awaitable<void>
consumer_plugin::initialize(fcl::app::plugin_context& context) {
   cache_ = context.apis().get<cache>({.id = {"cache"}, .major = 1, .min_revision = 8});
   auto chunk = co_await cache_->read(protocol::read_chunk{.ref = ref});
}
```

## Message-Oriented Frame

WebSocket, QUIC, P2P and TCP-like bindings use `fcl::api::frame`. HTTP does not
need to put this frame in the request body.

```cpp
auto request = fcl::api::frame{
   .kind = fcl::api::frame_kind::request,
   .id = {.value = 42},
   .api = {.id = {"cache"}, .major = 1, .min_revision = 8},
   .method = "read",
   .codec = {.value = "fcl.raw"},
};
fcl::raw::pack(request.payload, protocol::read_chunk{.ref = ref});
```

Frame lifecycle is checked by `fcl::api::call_runtime`:

- `request` opens a call id and must be unique while active.
- `response`, `error`, `cancel` and `stream_end` are terminal.
- `stream_item` keeps a streaming call active.
- unknown call ids, duplicate active ids and post-terminal frames are protocol
  errors.
- optional deadlines and max-inflight limits are enforced before dispatching the
  frame to product code.

Descriptor method kinds are explicit:

```cpp
return fcl::api::contract<cache>({.id = {"cache.events"}, .version = {1, 0}})
   .server_stream<&cache::watch, protocol::watch_chunks, models::chunk>("watch")
   .build();
```

Client and bidirectional streams use grouped frame dispatch. A client-stream
call starts with `request`, sends one or more `stream_item` frames with typed
request DTO payloads and finishes its input with `stream_end`; the server
returns a single `response` or `error`. A bidirectional stream follows the same
input shape and returns `stream_item...stream_end` or `error`.

```cpp
return fcl::api::contract<cache>({.id = {"cache.bulk"}, .version = {1, 0}})
   .client_stream<&cache::upload, protocol::write_chunk, protocol::write_receipt>("upload")
   .bidirectional_stream<&cache::sync, protocol::write_chunk, protocol::sync_event>("sync")
   .build();
```

## Interceptors

Interceptors are protocol-neutral API middleware. Use them for tracing,
authorization decisions, metrics and limits that should behave the same over
WebSocket, QUIC, P2P or an in-process test binding.

```cpp
auto plan = fcl::api::binding()
   .serve(app.apis())
   .interceptor(fcl::api::interceptor()
      .id("authz")
      .phase(fcl::api::interceptor_phase::authorize)
      .order(10)
      .handler([](fcl::api::call_context& call) -> boost::asio::awaitable<void> {
         co_await authorize_api_call(call.api, call.method, call.meta);
      })
      .build())
   .build();
```

HTTP-specific request middleware stays in `fcl.http.api::middleware(...)`;
API interceptors do not parse HTTP headers, routes or upgrade state.

## Error Payload

Typed FCL exceptions are projected to one shared DTO:

```json
{
  "error": "chunk_not_found",
  "message": "chunk not found",
  "retryable": false,
  "identity": {
    "category": "storlane.cache",
    "code": 1
  }
}
```

`identity` is stable machine-readable exception identity. Internal capture
context is diagnostic-only and is not returned externally by default.

Known remote errors can be restored to typed exceptions through the same method
descriptor:

```cpp
const auto payload = fcl::raw::unpack<fcl::api::error_payload>(frame.payload);
const auto* method = fcl::api::find_method(cache::describe(), frame.method);

try {
   fcl::api::throw_remote_error(payload, method);
} catch (const cache_errors::chunk_not_found& error) {
   // Handle the same typed exception shape as local plugin calls.
}
```

Unknown remote identities become `fcl::api::exceptions::remote_internal` with
the remote category/code preserved as redacted-safe context.

## Risks And Anti-Patterns

- Do not branch on exception message strings. Use typed exceptions or
  `identity.category/code`.
- Do not silently choose the first API implementation when version checks fail.
- Do not expose stack traces, secrets or capture context in network payloads.
- Do not force HTTP into a frame-only POST RPC shape; use native HTTP mapping in
  `fcl.http.api`.
- Do not add a builder option that only stores a flag. Any option exposed by API
  bindings must change behavior and have a test.

## Tests

`test_fcl_api` covers descriptor validation, local registry/view lookup, raw
frame dispatch, shared error payload serialization, declared typed exception
projection and typed remote exception restoration.
