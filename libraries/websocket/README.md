# forge_websocket

`forge_websocket` provides WebSocket client and connection primitives. Server-side
entry is intentionally through `forge_http` WebSocket upgrade routes so HTTP and
WebSocket share routing, TLS and lifecycle boundaries.

## When To Use

- A component needs bidirectional message streams over an HTTP-origin service.
- You need a reusable WebSocket connection abstraction with serialized writes.
- You need TLS WebSocket client support over the same runtime model as HTTP.

## When Not To Use

- Do not create application subscription/event semantics here.
- Do not introduce a standalone WebSocket server in v1; HTTP owns upgrade.
- Do not use WebSocket messages as an implicit authorization boundary.

## Public Modules

- `forge.websocket.connection` — connection pointer, reads/writes, close behavior.
- `forge.websocket.client` — client endpoint/options and connect helpers.

Target: `forge_websocket`.

Dependencies: `forge_asio`, Boost.Asio, Boost.Beast, OpenSSL.

## Examples

### Connect A Client

```cpp
#include <boost/asio/awaitable.hpp>

import forge.websocket.client;
import forge.websocket.connection;

auto endpoint = forge::websocket::client_endpoint{
   .host = "127.0.0.1",
   .port = "8443",
   .base_path = "/api",
   .tls = true,
};

auto client = forge::websocket::client{runtime, endpoint};
boost::asio::awaitable<void> connect_events(forge::websocket::client& client) {
   forge::websocket::connection::ptr connection = co_await client.async_connect("/events");
   use_connection(std::move(connection));
}
```

### Build A Target Path

```cpp
auto target = endpoint.make_target("events"); // "/api/events"
```

### HTTP Upgrade Route

```cpp
import forge.http.router;

router.websocket("/events", [](std::shared_ptr<forge::websocket::connection> connection) {
   connection->on_message([](forge::websocket::connection& ws, std::string message)
      -> boost::asio::awaitable<void> {
      co_await ws.send(std::move(message));
   });
   // forge::http::server starts the WebSocket read loop after this callback.
});
```

### Serve An API Session

`forge.websocket.api` uses `forge::api::frame` because WebSocket is
message-oriented and bidirectional. The binding is continuous: every inbound
WebSocket message is decoded as an API frame, checked against the configured
codec and frame-size limit, dispatched through `forge::api::frame_dispatcher`,
then replied with a response/error frame.

```cpp
import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.websocket.api;

auto plan = forge::api::binding()
   .serve(app.apis())
   .export_api<cache>({.id = {"cache"}, .major = 1, .min_revision = 8})
   .require_peer_api<client_session>({.id = {"client.session"}, .major = 1})
   .build();

auto binding = forge::websocket::api()
   .use(plan)
   .codec({"forge.raw"})
   .max_frame_size(1 << 20)
   .backpressure({.max_inflight = 128})
   .build();

router.websocket("/api", [binding](forge::websocket::connection::ptr connection) mutable {
   boost::asio::co_spawn(
      app.runtime().context(),
      binding.accept(std::move(connection)),
      boost::asio::detached);
});
```

`forge.websocket.api` owns API-level WebSocket binding behavior only:
max-frame-size rejection and handoff to the shared `frame_dispatcher`. It does
not use `forge.transport.api`, because WebSocket messages are not
`transport::stream` chunks. HTTP upgrade routes, TLS verification and application
reconnect policy stay with the transport owner.

### Send, Ping And Close

```cpp
import forge.websocket.connection;

boost::asio::awaitable<void> send_healthcheck(forge::websocket::connection::ptr connection) {
   co_await connection->send(R"({"type":"hello"})");
   co_await connection->ping("health");
   auto metrics = connection->metrics();
   record_metrics(metrics);
   co_await connection->close();
}
```

### Observe Close

```cpp
connection->on_close([](forge::websocket::connection& ws) {
   auto metrics = ws.metrics();
   record_disconnect(metrics.close_count);
});
```

## Security Notes

`client_options::verify_peer` defaults to `true`. Test-only insecure modes must
stay explicit and should not be hidden behind broad "dev" defaults.

## Risks And Anti-Patterns

- Do not treat a connected WebSocket as an authenticated session by itself.
  Application auth and replay rules live above the connection.
- Do not send concurrent writes through ad-hoc caller code if the connection
  does not serialize them. Use the FORGE connection API boundary.
- Do not put bearer tokens in query strings for convenience; they commonly leak
  through logs, metrics and diagnostics.
- Do not leave message handler exceptions to `co_spawn(..., detached)` with an
  empty completion handler. FORGE records handler failures and closes the handler
  path instead of silently swallowing correctness bugs.
- Do not invent WebSocket-specific error payloads for typed APIs; use
  `forge::api::error_payload` in error frames.
- Do not treat `.backpressure(...)` as a decorative value. If max inflight is
  exceeded, the API call runtime rejects the frame before application handlers run.
- Do not put HTTP upgrade or TLS policy in `forge.websocket.api`; it is an API
  binding over an already accepted connection.

## Typical Mistakes

- Do not perform concurrent writes directly against a connection unless the
  connection API serializes them.
- Do not leak bearer tokens in query strings when rendering endpoint diagnostics.
- Do not assume WebSocket reconnection is automatic application behavior; callers own
  policy.
- Do not make message handlers mutate shared state without a strand/serialization
  rule in the caller.

## Tests

`test_forge_http_websocket` covers WebSocket echo over the HTTP server port and TLS
client connection behavior.
