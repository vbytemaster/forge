# forge_transport_api

`forge_transport_api` binds typed `forge_api` contracts to `forge_transport`
streams and sessions. It does not own sockets, QUIC, P2P, WebSocket, HTTP,
plugins or application policy.

## When To Use

- Run a typed `forge_api` contract over an already established
  `forge_transport::stream` or session.
- Need request/response correlation, max-inflight limits and deadlines over a
  byte-stream transport.
- Build channel-specific API adapters such as QUIC or P2P bindings without
  duplicating frame dispatch.

## When Not To Use

- Do not use this library to open sockets, resolve peers or publish plugin
  lifecycle services. Establish the stream first.
- Do not use it for HTTP route/path/status semantics; use `forge_http_api`.
- Do not put application authorization, retry policy or large data-plane policy
  in the frame binding.

## Public Modules

- `forge.transport.api.connection`
- `forge.transport.api.options`
- `forge.transport.api.server`

## Dependencies

- `forge_api`
- `forge_raw`
- `forge_transport`
- Boost.Asio

## Responsibility

- `forge_api` owns contract descriptors, method dispatch, frame vocabulary and
  typed error projection.
- `forge_transport` owns byte streams, sessions, frame encoding and cancellation.
- `forge_transport_api` owns API frames over `transport::stream`: client pending
  calls, serialized writes, server frame loop, max-inflight limits, deadlines
  and close/cancel wakeups.
- `forge_quic.api` and `forge_p2p.api` are policy adapters over this layer.
- `forge_websocket.api` shares `forge::api::frame_dispatcher`, but not this layer,
  because WebSocket is message-oriented rather than a `transport::stream`.

## Examples

### Server

```cpp
import forge.api.types;
import forge.api.descriptor;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.transport.api.options;
import forge.transport.api.server;
#include <forge/api/macros.hpp>

class cache
   : public forge::api::contract<
        cache,
        forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~cache() = default;

   virtual boost::asio::awaitable<chunk>
   read(read_chunk request) = 0;
};

FORGE_API(cache, FORGE_API_CONTRACT("cache", 1, 0), FORGE_API_METHOD(read))

class cache_impl final : public cache {
 public:
   explicit cache_impl(cache_store& store) : store_{store} {}

   boost::asio::awaitable<chunk>
   read(read_chunk request) override {
      auto bytes = co_await store_.read_bytes(request.ref, request.offset, request.limit);
      co_return chunk{.bytes = std::move(bytes)};
   }

 private:
   cache_store& store_;
};

boost::asio::awaitable<void>
serve_cache(forge::transport::stream stream, cache_store& store) {
   auto apis = forge::api::registry{};
   apis.install<cache>(std::make_shared<cache_impl>(store));

   auto plan = forge::api::binding().serve(apis).build();

   co_await forge::transport::api::serve_stream(
      std::move(stream),
      std::move(plan),
      forge::transport::api::options{
         .codec = {.value = "forge.raw"},
         .max_inflight = 128,
         .deadline = std::chrono::seconds{5},
         .max_frame_size = 1024 * 1024,
      });
}
```

### Client

```cpp
import forge.api.types;
import forge.api.descriptor;
import forge.api.connection;
import forge.transport.api.options;
import forge.transport.api.connection;

boost::asio::awaitable<chunk>
read_remote(forge::transport::stream stream, std::string ref) {
   auto connection = forge::transport::api::connection{
      std::move(stream),
      forge::transport::api::options{
         .codec = {.value = "forge.raw"},
         .max_inflight = 64,
         .deadline = std::chrono::seconds{5},
      }};

   auto api = co_await connection.get_remote_api<cache>();

   co_return co_await api->read({
      .ref = std::move(ref),
      .offset = 0,
      .limit = 64 * 1024,
   });
}
```

## Notes

- The vector API remains the stable convenience path for typed DTO payloads.
- Large application data-plane policy stays above this layer; this binding only
  moves API frames over an already established stream/session.
- `serve_session(...)` owns admission through a Boost.Asio strand, so accepted
  stream slots and drain wakeups stay ordered on multi-worker runtimes. The
  full thread safety model is documented in
  [docs/runtime/thread-safety.md](../../docs/runtime/thread-safety.md).
- Do not add Peer ID, relay, discovery, HTTP routing or plugin lifecycle here.

## Security And Common Mistakes

- Enforce `max_frame_size`, `max_inflight` and deadlines. Do not leave remote
  peers with unbounded request bodies or pending calls.
- Do not reuse one connection object after the underlying stream has failed.
- Do not treat transport identity as application authorization. A caller above
  this layer must decide who may invoke an API.
- Do not add product-specific error DTOs. Use shared `forge::api::error_payload`.

## Tests

- `test_forge_transport_api`
