# fcl_http

`fcl_http` is the HTTP substrate: URL parsing, request/response aliases, routing,
middleware, server and client/connection primitives. It uses Boost.Beast/URL
internally but keeps FCL-owned route and lifecycle semantics.

## When To Use

- Build local or service HTTP APIs over Boost.Asio.
- Share routing and middleware with WebSocket upgrade handling.
- Use a queued per-connection HTTP client for serialized requests.

## When Not To Use

- Do not put application DTOs or JSON contracts in this library.
- Do not use HTTP as a security boundary by itself; auth belongs to consumers.
- Do not add a central application request queue here; request ownership remains
  at server/router/connection boundaries.

## Public Modules

- `fcl.http.types` — Beast-compatible request/response aliases.
- `fcl.http.base_url`, `fcl.http.target`.
- `fcl.http.router`, `fcl.http.route_context`, `fcl.http.middleware`.
- `fcl.http.client`, `fcl.http.connection`, `fcl.http.server`.
- `fcl.http` — aggregate import.

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
import fcl.http.router;
import fcl.http.types;

auto router = fcl::http::router{};
router.get("/healthz", [](fcl::http::route_context& ctx) {
   return fcl::http::make_text_response(ctx.request, fcl::http::status::ok, "ok");
});
```

### Mount API Bindings

`fcl.http.api` maps a typed `fcl_api` contract onto native HTTP routes. The
binding is a composable artifact; `build()` does not mutate the router.

```cpp
import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.http.api;
import fcl.http.router;

auto plan = fcl::api::binding()
   .serve(app.apis())
   .export_api<cache>({.id = {"cache"}, .major = 1, .min_revision = 8})
   .build();

auto binding = fcl::http::api(router)
   .use(plan)
   .post<&cache::write, protocol::write_chunk, protocol::write_receipt>(
      "/cache/chunks",
      {.success_status = fcl::http::status::created})
   .build();

router.mount(binding);
```

HTTP stays HTTP: route/path/status semantics remain native. Message-oriented
`fcl::api::frame` is not required as the HTTP body.

### Add Middleware

Low-level middleware can be installed directly on a router:

```cpp
router.use([](fcl::http::route_context& ctx, fcl::http::next_handler next) {
   if (ctx.request.find(fcl::http::field::authorization) == ctx.request.end()) {
      return fcl::http::make_text_response(
         ctx.request,
         fcl::http::status::unauthorized,
         "missing authorization");
   }
   return next();
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
      .handler = [](fcl::http::route_context& ctx, fcl::http::next_handler next) {
         authorize_cache_request(ctx.request);
         return next();
      },
   })
   .get<&cache::read, protocol::read_chunk, models::chunk>(
      "/cache/chunks/:ref",
      {.query = {"offset", "limit"}})
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
   {.bind_address = "127.0.0.1", .port = 8080},
   std::move(router),
};

server.start();
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
short-circuit requests and exceptions become typed HTTP responses at the route
boundary.

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
- Do not force all typed APIs into `POST /rpc`; use native HTTP route/status
  mapping where HTTP is the transport.
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

`test_fcl_http_websocket` covers base URL and target parsing, router matching,
middleware ordering, client/server roundtrip, reconnects and WebSocket upgrade.
