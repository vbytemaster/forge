# forge_api_transport

`forge_api_transport` binds typed `forge_api_core` contracts to
`forge_net_transport` connections and sessions. Shared API-over-stream serving
lives in `forge_api_stream`; this library adds the generic client,
connection and session layer. It does not own sockets, QUIC, P2P, WebSocket,
HTTP, plugins or application policy.

## When To Use

- Run a typed `forge_api_core` contract over an already established
  `forge_net_transport` connection or session.
- Need request/response correlation, max-inflight limits and deadlines over a
  byte-stream transport.
- Accept and serve generic transport sessions with admission/backpressure.

## When Not To Use

- Do not use this library to open sockets, resolve peers or publish plugin
  lifecycle services. Establish the stream first.
- Do not use it for HTTP route/path/status semantics; use `forge_api_http`.
- Do not put application authorization, retry policy or large data-plane policy
  in the frame binding.

## Public Modules

- `forge.api.transport.connection`
- `forge.api.transport.options`
- `forge.api.transport.server`

## Dependencies

- `forge_api_stream`
- `forge_net_transport`
- Boost.Asio

## Responsibility

- `forge_api_core` owns contract descriptors, method dispatch, frame vocabulary and
  typed error projection.
- `forge_net_transport` owns byte streams, sessions, frame encoding and cancellation.
- `forge_api_stream` owns the server-side frame loop over one
  `transport::stream`.
- `forge_api_transport` owns API clients/connections and session admission over
  `forge_net_transport`.
- `forge_api_quic` and `forge_api_p2p` use `forge_api_stream` directly for
  their stream adapters.
- `forge_api_websocket` adapts one binary WebSocket message to one transport
  frame and reuses the same `forge_api_stream` session runtime.

## Examples

### Server

```cpp
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.transport.options;
import forge.api.transport.server;
#include <forge/api/core/macros.hpp>

class cache
   : public forge::api::core::contract<
        cache,
        forge::api::core::surface::local | forge::api::core::surface::remote> {
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
serve_cache(forge::net::transport::stream stream, cache_store& store) {
   auto apis = forge::api::core::registry{};
   apis.install<cache>(std::make_shared<cache_impl>(store));

   auto plan = forge::api::core::binding().serve(apis).build();

   co_await forge::api::transport::serve_stream(
      std::move(stream),
      std::move(plan),
      forge::api::transport::options{
         .codec = {.value = "forge.raw"},
         .max_inflight = 128,
         .deadline = std::chrono::seconds{5},
         .max_frame_size = 1024 * 1024,
      });
}
```

### Client

```cpp
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.connection;
import forge.api.transport.options;
import forge.api.transport.connection;

boost::asio::awaitable<chunk>
read_remote(forge::net::transport::stream stream, std::string ref) {
   auto connection = forge::api::transport::connection{
      std::move(stream),
      forge::api::transport::options{
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

- `std::vector<T>` remains an ordinary unary DTO. Streaming methods use the
  endpoint and call APIs; there is no vector-shaped streaming fallback.
- Large application data-plane policy stays above this layer; this binding only
  moves API frames over an already established stream/session.
- `serve_session(...)` owns admission through a Boost.Asio strand, so accepted
  stream slots and drain wakeups stay ordered on multi-worker runtimes. The
  full thread safety model is documented in
  [docs/runtime/thread-safety.md](../../../docs/runtime/thread-safety.md).
- Do not add Peer ID, relay, discovery, HTTP routing or plugin lifecycle here.

## Security And Common Mistakes

- Enforce `max_frame_size`, `max_inflight` and deadlines. Do not leave remote
  peers with unbounded request bodies or pending calls.
- Do not reuse one connection object after the underlying stream has failed.
- Do not treat transport identity as application authorization. A caller above
  this layer must decide who may invoke an API.
- Do not add product-specific error DTOs. Use shared `forge::api::core::error_payload`.

## Tests

- `test_forge_api_transport`
