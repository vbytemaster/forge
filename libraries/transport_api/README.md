# fcl_transport_api

`fcl_transport_api` binds typed `fcl_api` contracts to `fcl_transport`
streams and sessions. It does not own sockets, QUIC, P2P, WebSocket, HTTP,
plugins or application policy.

## Responsibility

- `fcl_api` owns contract descriptors, method dispatch, frame vocabulary and
  typed error projection.
- `fcl_transport` owns byte streams, sessions, frame encoding and cancellation.
- `fcl_transport_api` owns API frames over `transport::stream`: client pending
  calls, serialized writes, server frame loop, max-inflight limits, deadlines
  and close/cancel wakeups.
- `fcl_quic.api` and `fcl_p2p.api` are policy adapters over this layer.
- `fcl_websocket.api` shares `fcl::api::frame_dispatcher`, but not this layer,
  because WebSocket is message-oriented rather than a `transport::stream`.

## Server Example

```cpp
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.transport.api.options;
import fcl.transport.api.server;
#include <fcl/api/macros.hpp>

class cache
   : public fcl::api::contract<
        cache,
        fcl::api::surface::local | fcl::api::surface::remote> {
 public:
   virtual ~cache() = default;

   virtual boost::asio::awaitable<chunk>
   read(read_chunk request) = 0;
};

FCL_API(cache, FCL_API_CONTRACT("cache", 1, 0), FCL_API_METHOD(read))

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
serve_cache(fcl::transport::stream stream, cache_store& store) {
   auto apis = fcl::api::registry{};
   apis.install<cache>(std::make_shared<cache_impl>(store));

   auto plan = fcl::api::binding().serve(apis).build();

   co_await fcl::transport::api::serve_stream(
      std::move(stream),
      std::move(plan),
      fcl::transport::api::options{
         .codec = {.value = "fcl.raw"},
         .max_inflight = 128,
         .deadline = std::chrono::seconds{5},
         .max_frame_size = 1024 * 1024,
      });
}
```

## Client Example

```cpp
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.connection;
import fcl.transport.api.options;
import fcl.transport.api.connection;

boost::asio::awaitable<chunk>
read_remote(fcl::transport::stream stream, std::string ref) {
   auto connection = fcl::transport::api::connection{
      std::move(stream),
      fcl::transport::api::options{
         .codec = {.value = "fcl.raw"},
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
