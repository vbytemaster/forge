# HTTP + WebSocket

`fcl_http` and `fcl_websocket` are separate libraries over the shared Asio
runtime. They are web/control surfaces, not the same layer as QUIC/P2P.

Local guides:

- [HTTP README](../../libraries/http/README.md)
- [WebSocket README](../../libraries/websocket/README.md)

## Задача

HTTP and WebSocket are often used for admin APIs, diagnostics, health checks,
browser-compatible integrations and local tooling. They need predictable routing,
timeouts, retries, upgrades and backpressure. Mixing this with peer transport
semantics creates blurry ownership and unsafe retry behavior.

## Layering

```text
fcl_asio::runtime
  -> fcl_http::server/router/middleware
      -> ordinary HTTP route handlers
      -> WebSocket Upgrade route
          -> fcl_websocket::connection
  -> fcl_http::client/connection
  -> fcl_websocket::client
```

`fcl_http` owns HTTP request/response mechanics and route matching.
`fcl_websocket` owns bidirectional message connection mechanics. Product DTOs,
authentication and business routing live above both.

## HTTP Decisions

- `base_url` describes the service origin and base path.
- `target` parsing is per-request and does not mutate the base URL.
- Server route handlers and middleware are `boost::asio::awaitable`-based.
- Server middleware runs in registration order and may short-circuit.
- Client requests are serialized through per-connection mechanics.
- Retry is only safe when explicitly idempotent.

## WebSocket Decisions

- Server-side WebSocket starts from HTTP Upgrade.
- There is no separate `websocket::server` in v1.
- Writes are serialized by connection-level mechanics.
- Reconnect/subscription policy is caller-owned.

## Integration Example

```cpp
auto router = fcl::http::router{};
router.get("/healthz", [](fcl::http::route_context& ctx)
   -> boost::asio::awaitable<fcl::http::response> {
   co_return fcl::http::make_text_response(ctx.request, fcl::http::status::ok, "ok");
});

router.websocket("/events", [](std::shared_ptr<fcl::websocket::connection> ws) {
   ws->on_message([](fcl::websocket::connection& connection, std::string message)
      -> boost::asio::awaitable<void> {
      co_await connection.send(std::move(message));
   });
   // fcl::http::server starts the WebSocket read loop after this callback.
});

auto server = fcl::http::server{
   runtime,
   {.bind_address = "127.0.0.1", .port = 8080},
   std::move(router),
};
server.start();
```

This example intentionally keeps auth, JSON DTOs and product actions outside
`fcl_http` and `fcl_websocket`.

## Security Boundary

HTTP/WebSocket do not provide authority by themselves. Authentication, bearer
tokens, cookies, API keys, role checks and redaction are responsibilities of
the consuming API layer. FCL must not log credentials from headers, query
strings or message bodies without explicit caller redaction.

## Failure Model

- Route miss -> 404.
- Method mismatch -> 405.
- Middleware/handler exception -> HTTP error response at route boundary.
- Remote close during client request -> typed connection failure; mutating
  retries are not automatic.
- WebSocket close is a connection lifecycle event, not a business event.

## Donor Decisions

Accepted:

- Boost.Beast request/response and close mechanics.
- Per-connection serialized writes.
- Shared HTTP Upgrade path for WebSocket.
- Metrics snapshots for reconnect/queue behavior.

Rejected:

- One central global server request queue.
- Implicit retry for mutating HTTP requests.
- Duplicated WebSocket server stack independent from HTTP routing.
- Parser/backend types as public API.

## Verification

`test_fcl_http_websocket` covers URL/target parsing, router behavior, middleware
short-circuiting, client/server roundtrip, reconnect rules, WebSocket upgrade
and TLS WebSocket client behavior.
