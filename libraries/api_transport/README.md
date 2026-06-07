# fcl_api_transport

`fcl_api_transport` binds typed `fcl_api` contracts to `fcl_transport`
streams and sessions. It does not own sockets, QUIC, P2P, WebSocket, HTTP,
plugins or product policy.

## Responsibility

- `fcl_api` owns contract descriptors, method dispatch, frame vocabulary and
  typed error projection.
- `fcl_transport` owns byte streams, sessions, frame encoding and cancellation.
- `fcl_api_transport` owns API frames over `transport::stream`: client pending
  calls, serialized writes, server frame loop, max-inflight limits, deadlines
  and close/cancel wakeups.
- `fcl_quic.api` and `fcl_p2p.api` are policy adapters over this layer.
- `fcl_websocket.api` shares `fcl::api::frame_dispatcher`, but not this layer,
  because WebSocket is message-oriented rather than a `transport::stream`.

## Server Example

```cpp
import fcl.api;
import fcl.api.transport;

class cache_impl final : public storlane::cache_api::cache {
 public:
   explicit cache_impl(local_branch& branch) : branch_{branch} {}

   boost::asio::awaitable<storlane::cache_api::chunk>
   read(storlane::cache_api::read_chunk request) override {
      auto bytes = co_await branch_.read_bytes(request.ref, request.offset, request.limit);
      co_return storlane::cache_api::chunk{.bytes = std::move(bytes)};
   }

 private:
   local_branch& branch_;
};

boost::asio::awaitable<void>
serve_cache(fcl::transport::stream stream, local_branch& branch) {
   auto apis = fcl::api::registry{};
   apis.install<storlane::cache_api::cache>(
      storlane::cache_api::cache::describe(),
      std::make_shared<cache_impl>(branch));

   auto plan = fcl::api::binding().serve(apis).build();

   co_await fcl::api::transport::serve_stream(
      std::move(stream),
      std::move(plan),
      fcl::api::transport::options{
         .codec = {.value = "fcl.raw"},
         .max_inflight = 128,
         .deadline = std::chrono::seconds{5},
         .max_frame_size = 1024 * 1024,
      });
}
```

## Client Example

```cpp
import fcl.api;
import fcl.api.transport;

class remote_cache final : public storlane::cache_api::cache {
 public:
   explicit remote_cache(fcl::api::transport::remote api) : api_{std::move(api)} {}

   boost::asio::awaitable<storlane::cache_api::chunk>
   read(storlane::cache_api::read_chunk request) override {
      co_return co_await api_.call<
         storlane::cache_api::read_chunk,
         storlane::cache_api::chunk>(
            {.id = {"storlane.cache"}, .major = 1, .min_revision = 0},
            "read",
            std::move(request));
   }

 private:
   fcl::api::transport::remote api_;
};

boost::asio::awaitable<storlane::cache_api::chunk>
read_remote(fcl::transport::stream stream, std::string ref) {
   auto client = fcl::api::transport::client{
      std::move(stream),
      fcl::api::transport::options{
         .codec = {.value = "fcl.raw"},
         .max_inflight = 64,
         .deadline = std::chrono::seconds{5},
      }};

   auto cache = remote_cache{
      fcl::api::transport::remote{std::move(client), storlane::cache_api::cache::describe()}};

   co_return co_await cache.read({
      .ref = std::move(ref),
      .offset = 0,
      .limit = 64 * 1024,
   });
}
```

## Notes

- The vector API remains the stable convenience path for typed DTO payloads.
- Large product data-plane policy stays above this layer; this binding only
  moves API frames over an already established stream/session.
- `serve_session(...)` owns admission through a Boost.Asio strand, so accepted
  stream slots and drain wakeups stay ordered on multi-worker runtimes. The
  full thread safety model is documented in
  [docs/runtime/thread-safety.md](../../docs/runtime/thread-safety.md).
- Do not add Peer ID, relay, discovery, HTTP routing or plugin lifecycle here.
