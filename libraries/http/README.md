# forge_http

`forge_http` is the HTTP substrate: URL parsing, FORGE-owned request/response
messages, streaming body primitives, routing, middleware, server and
client/connection primitives. It uses Boost.Beast/URL internally but keeps
FORGE-owned public message, route and lifecycle semantics.

Application-level server lifecycle can be owned directly with `forge::http::server`
or composed through the official `forge.plugins.http.server` plugin. The library
still owns HTTP mechanics; the plugin owns app lifecycle/config composition.

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

- `forge.http.types` — FORGE-owned Beast-like request/response wrappers, HTTP
  method/status enums and endpoint DTO state.
- `forge.http.body`, `forge.http.stream` — FORGE-owned chunk, reader, writer and
  stream route types.
- `forge.http.file`, `forge.http.range` — file responses, static roots and byte
  range parsing.
- `forge.http.negotiation` — generic media type and `Accept` header helpers for
  libraries that choose their own codecs above `forge_http`.
- `forge.http.upload` — upload reader, spill-to-disk spool and multipart form-data
  parsing.
- `forge.http.base_url`, `forge.http.target`.
- `forge.http.router`, `forge.http.route_context`, `forge.http.middleware`.
- `forge.http.client`, `forge.http.connection`, `forge.http.server`.

Target: `forge_http`.

Dependencies: `forge_asio`, `forge_websocket`, `forge_json`, `forge_schema`,
Boost.Asio, Boost.Beast, Boost.URL, OpenSSL.

Boost.Beast remains the runtime donor and backend for parser/serializer/socket
mechanics, but public HTTP APIs use `forge::http::request` and
`forge::http::response` wrappers rather than Beast message aliases.

Typed `FORGE_HTTP_API(...)` route binding lives in the separate `forge_http_api`
target/component. Its public modules are `forge.http.api.binding`,
`forge.http.api.mapping` and `forge.http.api.proxy`; its macro header is
`<forge/http_api/macros.hpp>`.

## Examples

### Parse Base URL

```cpp
import forge.http.base_url;

auto endpoint = forge::http::parse_base_url("https://127.0.0.1:8443/api");
auto target = endpoint.make_target("/healthz"); // "/api/healthz"
```

### Parse A Request Target

```cpp
import forge.http.target;

auto parsed = forge::http::parse_target("/v1/items?limit=10&cursor=abc");
auto first_segment = parsed.segments.front(); // "v1"
auto query = parsed.query_params.front();
```

### Route Requests

```cpp
#include <boost/asio/awaitable.hpp>

import forge.http.router;
import forge.http.types;

auto router = forge::http::router{};
router.get("/healthz", [](forge::http::route_context& ctx)
   -> boost::asio::awaitable<forge::http::response> {
   co_return forge::http::make_text_response(ctx.request, forge::http::status::ok, "ok");
});
```

### Use Endpoint Request State

Typed HTTP request DTOs may derive from `forge::http::endpoint_request` when a
handler needs read-only access to the incoming HTTP request or wants to add
response metadata. The base is not described with Boost.Describe and is ignored
by JSON/schema binding.

```cpp
struct read_request : forge::http::endpoint_request {
   std::string ref;
};

BOOST_DESCRIBE_STRUCT(read_request, (), (ref))

boost::asio::awaitable<chunk>
cache_impl::read(read_request request) {
   auto trace = request.request().header("X-Trace").value_or("");
   request.response().set("Cache-Control", "public, max-age=60");
   request.response().set_cookie("trace", trace);
   co_return load_chunk(request.ref);
}
```

### Route Streaming Bodies

Use stream routes for upload/download mechanics that should be visible as HTTP
body flow, not as `FORGE_API` DTO calls. The server routes after headers are read;
middleware can reject before the body is consumed. For requests with
`Expect: 100-continue`, `forge_http` sends the interim `100 Continue` response
only when the route actually starts reading the body, so header-only rejection
does not force a large upload.

```cpp
import forge.http.body;
import forge.http.router;
import forge.http.stream;

router.post_stream("/upload", [](forge::http::stream_request& req)
   -> boost::asio::awaitable<forge::http::stream_response> {
   std::uint64_t received = 0;
   while (auto chunk = co_await req.body.async_read()) {
      received += chunk->bytes.size();
      consume_upload_chunk(chunk->bytes);
   }

   co_return forge::http::stream_response::buffered(
      forge::http::make_text_response(req.context.request, forge::http::status::ok, "stored"));
});

router.get_stream("/download", [](forge::http::stream_request& req)
   -> boost::asio::awaitable<forge::http::stream_response> {
   auto source = open_chunk_source(req.context.request);
   auto head = forge::http::response{forge::http::status::ok, req.context.request.version()};
   head.set(forge::http::field::content_type, "application/octet-stream");

   co_return forge::http::stream_response{
      .head = std::move(head),
      .body = [source = std::move(source)]() mutable
         -> boost::asio::awaitable<std::optional<forge::http::body_chunk>> {
         co_return co_await source.next_chunk();
      },
   };
});
```

Stream routes provide FORGE-owned body readers and response body sources. Use
`forge.http.upload` when the request body should be bounded, optionally spooled to
disk, or parsed as browser-style `multipart/form-data`.

### Negotiate Content Types

`forge.http.negotiation` parses media types and `Accept` headers generically.
It understands parameters, structured suffixes such as `+json`, wildcard media
ranges and `q=0` exclusion. Codec ownership stays outside `forge_http`; callers
provide the media types they support.

```cpp
import forge.http.negotiation;

constexpr auto xml = std::array{
   forge::http::media_type_match{.type = "application/xml", .structured_suffix = "+xml"},
   forge::http::media_type_match{.type = "text/xml"},
};

auto content_ok = forge::http::media_type_matches("application/custom+xml", xml);
auto accept_ok = forge::http::accept_allows("application/json;q=0, application/xml;q=1", xml);
```

### Read Uploads

`upload_reader` consumes a `body_reader` incrementally. Small payloads stay in
memory; larger payloads spill to an owner-private temporary file that is removed
when the returned `upload_part` is destroyed unless the caller explicitly
releases the spool.

```cpp
import forge.http.upload;

router.post_stream("/upload", [](forge::http::stream_request& req)
   -> boost::asio::awaitable<forge::http::stream_response> {
   auto reader = forge::http::upload_reader{
      std::move(req.body),
         forge::http::upload_options{
            .memory_threshold_bytes = 1 * 1024 * 1024,
            .max_file_bytes = 64 * 1024 * 1024,
            .max_field_bytes = 1 * 1024 * 1024,
            .max_total_bytes = 128 * 1024 * 1024,
         },
   };

   auto part = co_await reader.async_read();
   consume_upload(part);

   co_return forge::http::stream_response::buffered(
      forge::http::make_text_response(req.context.request, forge::http::status::ok, "stored"));
});
```

`async_read_multipart(content_type)` parses browser-style form uploads into
fields and file parts. It is not an object-storage multipart workflow; object
storage state machines belong above `forge_http`.

Multipart limits are separate: `max_total_bytes` bounds the whole envelope,
`max_file_bytes` bounds each file part, and `max_field_bytes` bounds each
non-file field. `upload_part::filename` is untrusted client metadata; use
`safe_filename()` as a conservative basename or apply a stricter product policy
before using any uploaded name in a filesystem path.

### Serve Static Files And Ranges

`static_file_root` serves files through the stream response path, with root path
normalization, traversal rejection, configurable symlink policy, `HEAD`, byte
ranges and conditional metadata headers.

```cpp
import forge.http.file;
import forge.http.router;
import forge.http.stream;

auto files = std::make_shared<forge::http::static_file_root>(
   "/srv/public",
   forge::http::file_options{
      .content_type = "application/octet-stream",
      .symlinks = forge::http::symlink_policy::reject,
   });

router.get_stream("/files/:name", [files](forge::http::stream_request& req)
   -> boost::asio::awaitable<forge::http::stream_response> {
   co_return co_await files->serve(req, *req.context.route_param("name"));
});

router.head_stream("/files/:name", [files](forge::http::stream_request& req)
   -> boost::asio::awaitable<forge::http::stream_response> {
   co_return co_await files->serve(req, *req.context.route_param("name"));
});
```

This is a file-serving foundation, not a storage product. Object metadata,
authorization, placement and compatibility-specific error shapes belong above
`forge_http`.

### Mount API Bindings

`FORGE_HTTP_API(...)` maps a typed `FORGE_API(...)` contract onto native HTTP routes.
The binding is a composable artifact; `build()` does not mutate the router.

```cpp
#include <forge/api/macros.hpp>
#include <forge/http_api/macros.hpp>

import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.http.api.binding;
import forge.http.api.proxy;
import forge.http.router;

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

class cache : public forge::api::contract<
   cache,
   forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~cache() = default;

   virtual boost::asio::awaitable<chunk> read(read_chunk request) = 0;
   virtual boost::asio::awaitable<chunk> write(write_chunk request) = 0;
};

FORGE_API(
   cache,
   FORGE_API_CONTRACT("cache", 1, 0),
   FORGE_API_METHOD(read),
   FORGE_API_METHOD(write))

FORGE_HTTP_API(
   cache,
   FORGE_HTTP_GET(read, "/cache/chunks/:ref?offset={offset}&limit={limit}"),
   FORGE_HTTP_PUT(write, "/cache/chunks/:ref", created))

auto plan = forge::api::binding()
   .serve(app.apis())
   .export_api<cache>()
   .build();

auto binding = forge::http::api::binding()
   .use(plan)
   .bind<cache>()
   .build();

router.mount(binding);
```

HTTP stays HTTP: route/path/status semantics remain native. The transport does
not wrap typed calls in a message-frame body.

### FastAPI-Style DTO Parameters

For production HTTP endpoints, prefer one described request DTO. FastAPI-style
parameter categories live as DTO fields, not as a long positional method
signature. This keeps call sites readable, keeps validation paths named, and
keeps the HTTP-specific surface out of `forge_api`.

```cpp
struct write_payload {
   std::string bytes;
};

struct write_receipt {
   std::string id;
};

BOOST_DESCRIBE_STRUCT(write_payload, (), (bytes))
BOOST_DESCRIBE_STRUCT(write_receipt, (), (id))

struct put_object_request {
   std::string bucket;
   std::string key;
   forge::http::query<std::uint32_t> ttl;
   forge::http::header<std::string> request_id;
   forge::http::body<write_payload> body;
};

BOOST_DESCRIBE_STRUCT(put_object_request, (), (bucket, key, ttl, request_id, body))

class object_api : public forge::api::contract<object_api> {
 public:
   virtual ~object_api() = default;

   virtual boost::asio::awaitable<write_receipt>
   put_object(put_object_request request) = 0;
};

FORGE_API(
   object_api,
   FORGE_API_CONTRACT("object", 1, 0),
   FORGE_API_METHOD(put_object))

FORGE_HTTP_API(
   object_api,
   FORGE_HTTP_PUT(put_object, "/objects/:bucket/:key?ttl={ttl}", created,
      FORGE_HTTP_HEADER(request_id, "X-Request-Id")))
```

Server binding fills `bucket` and `key` from path placeholders, `ttl` from the
query string, `request_id` from `X-Request-Id`, and `body` from a JSON request
body. If a wire header or form name must differ from the DTO field name, use
the existing route options such as `FORGE_HTTP_HEADER(field, "Wire-Name")` or
`FORGE_HTTP_FORM(field, "wire-name")`.

The same typed client call builds the HTTP request:

```cpp
auto objects = co_await forge::http::api::remote<object_api>(client);
auto receipt = co_await objects->put_object({
   .bucket = "cache",
   .key = "chunk-1",
   .ttl = {.value = 3600, .present = true},
   .request_id = {.value = "trace-123", .present = true},
   .body = {.value = {.bytes = "payload"}, .present = true},
});
```

HTTP-only special request types include `query<T>`, `header<T>`, `cookie<T>`,
`body<T>`, `form<T>`, `form_field<T>`, `upload_file`, `body_bytes` and
`body_stream`, and they are supported as fields of a described request DTO.
The typed HTTP client supports JSON, raw bytes, streaming body and browser-style
multipart DTO fields without routing these wrappers through `forge.raw`.

HTTP positional methods remain available only as small routing sugar: scalar,
string, enum and optional arguments may bind to route path/query placeholders,
and at most one remaining described DTO argument may become the JSON body for a
body-capable route. HTTP wrappers such as `query<T>`, `header<T>`,
`body_stream`, `form<T>` and `upload_file` are not allowed in positional HTTP
signatures. Multi-argument APIs remain first-class for local, WebSocket, QUIC,
P2P, TCP and STCP bindings where there is no HTTP parameter model.

Special return types remain `file_response`, `streaming_response`,
`bytes_response` and `empty_response`. Background task injection is
intentionally out of scope; application runtime and plugin layers own
background work.

### Add Middleware

Low-level middleware can be installed directly on a router:

```cpp
router.use([](forge::http::route_context& ctx, forge::http::next_handler next)
   -> boost::asio::awaitable<forge::http::response> {
   if (ctx.request.find(forge::http::field::authorization) == ctx.request.end()) {
      co_return forge::http::make_text_response(
         ctx.request,
         forge::http::status::unauthorized,
         "missing authorization");
   }
   co_return co_await next();
});
```

Typed API bindings should contribute middleware through the binding artifact so
route plugins can be composed before the server starts:

```cpp
auto binding = forge::http::api::binding()
   .use(plan)
   .middleware(forge::http::middleware_descriptor{
      .id = "cache.authz",
      .phase = forge::http::middleware_phase::security,
      .order = 100,
      .path_prefix = "/cache",
      .handler = [](forge::http::route_context& ctx, forge::http::next_handler next)
         -> boost::asio::awaitable<forge::http::response> {
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
import forge.asio.runtime;
import forge.http.server;

auto runtime = forge::asio::runtime{};
auto server = forge::http::server{
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

import forge.http.client;
import forge.http.types;

boost::asio::awaitable<void> check_ready(forge::http::client& client) {
   forge::http::response response = co_await client.async_get("/readyz");
   if (response.result() != forge::http::status::ok) {
      report_http_error(response.result(), response.body());
   }
}
```

### Use A Typed HTTP API

```cpp
#include <boost/asio/awaitable.hpp>

import forge.api.handle;
import forge.http.client;
import forge.http.api.proxy;

boost::asio::awaitable<void> read_chunk(forge::http::client& client) {
   forge::api::handle<cache> cache_api = co_await forge::http::api::remote<cache>(client);
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

import forge.http.client;
import forge.http.types;
import forge.json;

struct action_request {
   bool dry_run = false;
};

BOOST_DESCRIBE_STRUCT(action_request, (), (dry_run))

boost::asio::awaitable<void> submit_action(forge::http::client& client) {
   auto body = forge::json::write(action_request{.dry_run = true});
   if (!body.ok()) {
      report_diagnostics(body.diagnostics);
      co_return;
   }

   forge::http::response response = co_await client.async_post_json("/v1/actions", body.text);
   if (response.result() != forge::http::status::ok) {
      handle_http_error(response.result(), response.body());
   }
}
```

Raw JSON string literals are fine for tests and probes, but application APIs should
prefer described DTOs plus `forge_json` so field names and diagnostics stay in one
place.

### WebSocket Upgrade Route

```cpp
import forge.websocket.connection;

router.websocket("/events", [](std::shared_ptr<forge::websocket::connection> ws) {
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
  consumer auth service, but application policy lives above `forge_http`.
- Do not retry mutating requests implicitly. The caller must decide whether an
  operation is idempotent and safe to replay.
- Do not log request bodies, headers or query strings before redaction. They may
  contain credentials or user data.
- Do not catch application exceptions in every route by hand. Prefer typed
  `forge_exceptions` categories and let API bindings project them to
  `forge::api::error_payload`.
- Do not force all typed APIs into a single generic RPC endpoint; use native HTTP route/status
  mapping where HTTP is the transport.
- Do not force file upload/download through `FORGE_API`; use stream routes and the
  file/upload helper layers.
- Do not hide server bind/TLS/lifecycle in `forge.http.api.binding`; the API builder owns
  route mapping, API middleware, status projection and error payloads only.
- Do not add HTTP API builder options unless they change runtime behavior and
  have tests.

## Typical Mistakes

- Do not parse full base URLs for every request target; use `base_url` for the
  origin and `target` for per-request paths.
- Do not put WebSocket server lifecycle in a separate `websocket::server`; v1
  upgrade starts from the HTTP server/router.
- Do not log headers or bodies containing credentials without redaction.
- Do not put authentication policy in `forge_http`; middleware can call a consumer
  auth service, but the policy owner is outside this library.

## Tests

`test_forge_http_websocket` covers base URL and target parsing, async router and
middleware behavior, stream request/response bodies, typed HTTP API mapping,
client/server roundtrip, reconnects and WebSocket upgrade.
