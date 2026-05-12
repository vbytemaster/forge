# fcl_http

`fcl_http` is the HTTP substrate: URL parsing, request/response aliases, routing,
middleware, server and client/connection primitives. It uses Boost.Beast/URL
internally but keeps FCL-owned route and lifecycle semantics.

## When To Use

- Build local or service HTTP APIs over Boost.Asio.
- Share routing and middleware with WebSocket upgrade handling.
- Use a queued per-connection HTTP client for serialized requests.

## When Not To Use

- Do not put product DTOs or JSON contracts in this library.
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

### Add Middleware

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

### Start A Local Server

```cpp
import fcl.asio.runtime;
import fcl.http.server;

auto runtime = fcl::asio::runtime{};
auto server = fcl::http::server{
   runtime,
   {.bind_address = "127.0.0.1", .port = 0},
   std::move(router),
};

server.start();
auto bound_port = server.port();
```

### Use The Client

```cpp
import fcl.http.client;

auto client = fcl::http::client{runtime, endpoint};
auto response = co_await client.async_get("/readyz");
```

### Send JSON Without Owning JSON DTOs

```cpp
auto response = co_await client.async_post_json(
   "/v1/actions",
   R"({"dry_run":true})");

if (response.result() != fcl::http::status::ok) {
   handle_http_error(response.result(), response.body());
}
```

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
