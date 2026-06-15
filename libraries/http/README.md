# fcl_http

`fcl_http` is the HTTP substrate: URL parsing, request/response aliases,
streaming body primitives, routing, middleware, server and client/connection
primitives. It uses Boost.Beast/URL internally but keeps FCL-owned route and
lifecycle semantics.

Application-level server lifecycle is usually owned by
`fcl.plugins.http_server.plugin`; use the library directly when building a custom
HTTP host or testing route/middleware behavior without the app plugin layer.

## When To Use

- Build local or service HTTP APIs over Boost.Asio.
- Share routing and middleware with WebSocket upgrade handling.
- Use a queued per-connection HTTP client for serialized requests.
- Publish HTTP-native stream routes when request/response bodies should not be
  forced through a full in-memory DTO path.

## When Not To Use

- Do not put application DTOs or JSON contracts in this library.
- Do not use HTTP as a security boundary by itself; auth belongs to consumers.
- Do not add a central application request queue here; request ownership remains
  at server/router/connection boundaries.

## Public Modules

- `fcl.http.types` — Beast-compatible request/response aliases.
- `fcl.http.body`, `fcl.http.stream` — FCL-owned chunk, reader, writer and
  stream route types.
- `fcl.http.file`, `fcl.http.range` — file responses, static roots and byte
  range parsing.
- `fcl.http.base_url`, `fcl.http.target`.
- `fcl.http.router`, `fcl.http.route_context`, `fcl.http.middleware`.
- `fcl.http.api`, `fcl.http.mapping`, `fcl.http.proxy`.
- `fcl.http.client`, `fcl.http.connection`, `fcl.http.server`.
- Macro header: `<fcl/http/macros.hpp>` for `FCL_HTTP_API(...)`.

Target: `fcl_http`.

Dependencies: `fcl_asio`, `fcl_websocket`, Boost.Asio, Boost.Beast, Boost.URL,
OpenSSL.

## Examples

### Parse Base URL

```cpp
import fcl.http.base_url;

auto endpoint = fcl::http::parse_base_url("https://127.0.0.1:8443/api");
auto target = endpoint.make_target("/healthz"); // "/api/healthz"
```

### Parse A Request Target

```cpp
import fcl.http.target;

auto parsed = fcl::http::parse_target("/v1/items?limit=10&cursor=abc");
auto first_segment = parsed.segments.front(); // "v1"
auto query = parsed.query_params.front();
```

### Route Requests

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.http.router;
import fcl.http.types;

auto router = fcl::http::router{};
router.get("/healthz", [](fcl::http::route_context& ctx)
   -> boost::asio::awaitable<fcl::http::response> {
   co_return fcl::http::make_text_response(ctx.request, fcl::http::status::ok, "ok");
});
```

### Route Streaming Bodies

Use stream routes for upload/download mechanics that should be visible as HTTP
body flow, not as `FCL_API` DTO calls. The server routes after headers are read;
middleware can reject before the body is consumed.

```cpp
import fcl.http.body;
import fcl.http.router;
import fcl.http.stream;

router.post_stream("/upload", [](fcl::http::stream_request& req)
   -> boost::asio::awaitable<fcl::http::stream_response> {
   std::uint64_t received = 0;
   while (auto chunk = co_await req.body.async_read()) {
      received += chunk->bytes.size();
      consume_upload_chunk(chunk->bytes);
   }

   co_return fcl::http::stream_response::buffered(
      fcl::http::make_text_response(req.context.request, fcl::http::status::ok, "stored"));
});

router.get_stream("/download", [](fcl::http::stream_request& req)
   -> boost::asio::awaitable<fcl::http::stream_response> {
   auto source = open_chunk_source(req.context.request);
   auto head = fcl::http::response{fcl::http::status::ok, req.context.request.version()};
   head.set(fcl::http::field::content_type, "application/octet-stream");

   co_return fcl::http::stream_response{
      .head = std::move(head),
      .body = [source = std::move(source)]() mutable
         -> boost::asio::awaitable<std::optional<fcl::http::body_chunk>> {
         co_return co_await source.next_chunk();
      },
   };
});
```

Stream routes currently provide FCL-owned body readers and response body
sources. Higher-level upload helpers are a separate follow-up layer; do not
hand-roll downstream object-storage gateway policy or raw router mutation in
this library.

### Serve Static Files And Ranges

`static_file_root` serves files through the stream response path, with root path
normalization, traversal rejection, configurable symlink policy, `HEAD`, byte
ranges and conditional metadata headers.

```cpp
import fcl.http.file;
import fcl.http.router;
import fcl.http.stream;

auto files = std::make_shared<fcl::http::static_file_root>(
   "/srv/public",
   fcl::http::file_options{
      .content_type = "application/octet-stream",
      .symlinks = fcl::http::symlink_policy::reject,
   });

router.get_stream("/files/:name", [files](fcl::http::stream_request& req)
   -> boost::asio::awaitable<fcl::http::stream_response> {
   co_return co_await files->serve(req, *req.context.route_param("name"));
});

router.head_stream("/files/:name", [files](fcl::http::stream_request& req)
   -> boost::asio::awaitable<fcl::http::stream_response> {
   co_return co_await files->serve(req, *req.context.route_param("name"));
});
```

This is a file-serving foundation, not a storage product. Object metadata,
authorization, placement and compatibility-specific error shapes belong above
`fcl_http`.

### Mount API Bindings

`FCL_HTTP_API(...)` maps a typed `FCL_API(...)` contract onto native HTTP routes.
The binding is a composable artifact; `build()` does not mutate the router.

```cpp
#include <fcl/api/macros.hpp>
#include <fcl/http/macros.hpp>

import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.http.api;
import fcl.http.proxy;
import fcl.http.router;

struct read_chunk {
   std::string ref;
   std::uint32_t offset = 0;
   std::uint32_t limit = 0;
};

struct write_chunk {
   std::string ref;
   std::string bytes;
};

struct chunk {
   std::string bytes;
};

class cache : public fcl::api::contract<
   cache,
   fcl::api::surface::local | fcl::api::surface::remote> {
 public:
   virtual ~cache() = default;

   virtual boost::asio::awaitable<chunk> read(read_chunk request) = 0;
   virtual boost::asio::awaitable<chunk> write(write_chunk request) = 0;
};

FCL_API(
   cache,
   FCL_API_CONTRACT("cache", 1, 0),
   FCL_API_METHOD(read),
   FCL_API_METHOD(write))

FCL_HTTP_API(
   cache,
   FCL_HTTP_GET(read, "/cache/chunks/:ref?offset={offset}&limit={limit}"),
   FCL_HTTP_PUT(write, "/cache/chunks/:ref", created))

auto plan = fcl::api::binding()
   .serve(app.apis())
   .export_api<cache>()
   .build();

auto binding = fcl::http::api()
   .use(plan)
   .bind<cache>()
   .build();

router.mount(binding);
```

HTTP stays HTTP: route/path/status semantics remain native. Message-oriented
`fcl::api::frame` is not required as the HTTP body.

### Add Middleware

Low-level middleware can be installed directly on a router:

```cpp
router.use([](fcl::http::route_context& ctx, fcl::http::next_handler next)
   -> boost::asio::awaitable<fcl::http::response> {
   if (ctx.request.find(fcl::http::field::authorization) == ctx.request.end()) {
      co_return fcl::http::make_text_response(
         ctx.request,
         fcl::http::status::unauthorized,
         "missing authorization");
   }
   co_return co_await next();
});
```

Typed API bindings should contribute middleware through the binding artifact so
route plugins can be composed before the server starts:

```cpp
auto binding = fcl::http::api()
   .use(plan)
   .middleware(fcl::http::middleware_descriptor{
      .id = "cache.authz",
      .phase = fcl::http::middleware_phase::security,
      .order = 100,
      .path_prefix = "/cache",
      .handler = [](fcl::http::route_context& ctx, fcl::http::next_handler next)
         -> boost::asio::awaitable<fcl::http::response> {
         authorize_cache_request(ctx.request);
         co_return co_await next();
      },
   })
   .bind<cache>()
   .build();

router.mount(binding);
```

Middleware contributions are sorted by `phase`, `order` and `id`. Duplicate
middleware ids and duplicate routes fail deterministically during
`router.mount(binding)`, before serving traffic.

### Start A Local Server

```cpp
import fcl.asio.runtime;
import fcl.http.server;

auto runtime = fcl::asio::runtime{};
auto server = fcl::http::server{
   runtime,
   {
      .bind_address = "127.0.0.1",
      .port = 8080,
      .max_request_body_bytes = 16 * 1024 * 1024,
      .max_header_bytes = 64 * 1024,
      .read_timeout = 30s,
      .idle_timeout = 120s,
   },
   std::move(router),
};

co_await server.async_start();
```

### Use The Client

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.http.client;
import fcl.http.types;

boost::asio::awaitable<void> check_ready(fcl::http::client& client) {
   fcl::http::response response = co_await client.async_get("/readyz");
   if (response.result() != fcl::http::status::ok) {
      report_http_error(response.result(), response.body());
   }
}
```

### Use A Typed HTTP API

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.api.handle;
import fcl.http.client;
import fcl.http.proxy;

boost::asio::awaitable<void> read_chunk(fcl::http::client& client) {
   fcl::api::handle<cache> cache_api = co_await fcl::http::remote<cache>(client);
   chunk value = co_await cache_api->read({
      .ref = "abc",
      .offset = 0,
      .limit = 64 * 1024,
   });
   consume(value);
}
```

### Send A JSON DTO

```cpp
#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

import fcl.http.client;
import fcl.http.types;
import fcl.json;

struct action_request {
   bool dry_run = false;
};

BOOST_DESCRIBE_STRUCT(action_request, (), (dry_run))

boost::asio::awaitable<void> submit_action(fcl::http::client& client) {
   auto body = fcl::json::write(action_request{.dry_run = true});
   if (!body.ok()) {
      report_diagnostics(body.diagnostics);
      co_return;
   }

   fcl::http::response response = co_await client.async_post_json("/v1/actions", body.text);
   if (response.result() != fcl::http::status::ok) {
      handle_http_error(response.result(), response.body());
   }
}
```

Raw JSON string literals are fine for tests and probes, but application APIs should
prefer described DTOs plus `fcl_json` so field names and diagnostics stay in one
place.

### WebSocket Upgrade Route

```cpp
import fcl.websocket.connection;

router.websocket("/events", [](std::shared_ptr<fcl::websocket::connection> ws) {
   // Own the connection lifecycle in the caller.
});
```

## Backpressure And Failure Model

Client requests are serialized through a per-connection queue. Retry behavior is
restricted to safe/idempotent cases covered by tests. Middleware can
short-circuit stream routes after headers and before body consumption.
Exceptions become typed HTTP responses at the route boundary, and stream body
limits/timeouts apply while chunks are read.

## Risks And Anti-Patterns

- Do not use HTTP routes as the authorization boundary. Middleware may call a
  consumer auth service, but application policy lives above `fcl_http`.
- Do not retry mutating requests implicitly. The caller must decide whether an
  operation is idempotent and safe to replay.
- Do not log request bodies, headers or query strings before redaction. They may
  contain credentials or user data.
- Do not catch application exceptions in every route by hand. Prefer typed
  `fcl_exceptions` categories and let API bindings project them to
  `fcl::api::error_payload`.
- Do not force all typed APIs into a single generic RPC endpoint; use native HTTP route/status
  mapping where HTTP is the transport.
- Do not force file upload/download through `FCL_API`; use stream routes and the
  future file/upload helper layers.
- Do not hide server bind/TLS/lifecycle in `fcl.http.api`; the API builder owns
  route mapping, API middleware, status projection and error payloads only.
- Do not add HTTP API builder options unless they change runtime behavior and
  have tests.

## Typical Mistakes

- Do not parse full base URLs for every request target; use `base_url` for the
  origin and `target` for per-request paths.
- Do not put WebSocket server lifecycle in a separate `websocket::server`; v1
  upgrade starts from the HTTP server/router.
- Do not log headers or bodies containing credentials without redaction.
- Do not put authentication policy in `fcl_http`; middleware can call a consumer
  auth service, but the policy owner is outside this library.

## Tests

`test_fcl_http_websocket` covers base URL and target parsing, async router and
middleware behavior, stream request/response bodies, typed HTTP API mapping,
client/server roundtrip, reconnects and WebSocket upgrade.
