# fcl_websocket

`fcl_websocket` provides WebSocket client and connection primitives. Server-side
entry is intentionally through `fcl_http` WebSocket upgrade routes so HTTP and
WebSocket share routing, TLS and lifecycle boundaries.

## When To Use

- A component needs bidirectional message streams over an HTTP-origin service.
- You need a reusable WebSocket connection abstraction with serialized writes.
- You need TLS WebSocket client support over the same runtime model as HTTP.

## When Not To Use

- Do not create product subscription/event semantics here.
- Do not introduce a standalone WebSocket server in v1; HTTP owns upgrade.
- Do not use WebSocket messages as an implicit authorization boundary.

## Public Modules

- `fcl.websocket.connection` — connection pointer, reads/writes, close behavior.
- `fcl.websocket.client` — client endpoint/options and connect helpers.
- `fcl.websocket` — aggregate import.

Target: `fcl_websocket`.

Dependencies: `fcl_asio`, Boost.Asio, Boost.Beast, OpenSSL.

## Examples

### Connect A Client

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.websocket.client;
import fcl.websocket.connection;

auto endpoint = fcl::websocket::client_endpoint{
   .host = "127.0.0.1",
   .port = "8443",
   .base_path = "/api",
   .tls = true,
};

auto client = fcl::websocket::client{runtime, endpoint};
boost::asio::awaitable<void> connect_events(fcl::websocket::client& client) {
   fcl::websocket::connection::ptr connection = co_await client.async_connect("/events");
   use_connection(std::move(connection));
}
```

### Build A Target Path

```cpp
auto target = endpoint.make_target("events"); // "/api/events"
```

### HTTP Upgrade Route

```cpp
import fcl.http.router;

router.websocket("/events", [](std::shared_ptr<fcl::websocket::connection> connection) {
   connection->on_message([](fcl::websocket::connection& ws, std::string message)
      -> boost::asio::awaitable<void> {
      co_await ws.send(std::move(message));
   });
   // fcl::http::server starts the WebSocket read loop after this callback.
});
```

### Send, Ping And Close

```cpp
import fcl.websocket.connection;

boost::asio::awaitable<void> send_healthcheck(fcl::websocket::connection::ptr connection) {
   co_await connection->send(R"({"type":"hello"})");
   co_await connection->ping("health");
   auto metrics = connection->metrics();
   record_metrics(metrics);
   co_await connection->close();
}
```

### Observe Close

```cpp
connection->on_close([](fcl::websocket::connection& ws) {
   auto metrics = ws.metrics();
   record_disconnect(metrics.close_count);
});
```

## Security Notes

`client_options::verify_peer` defaults to `true`. Test-only insecure modes must
stay explicit and should not be hidden behind broad "dev" defaults.

## Risks And Anti-Patterns

- Do not treat a connected WebSocket as an authenticated session by itself.
  Product auth and replay rules live above the connection.
- Do not send concurrent writes through ad-hoc caller code if the connection
  does not serialize them. Use the FCL connection API boundary.
- Do not put bearer tokens in query strings for convenience; they commonly leak
  through logs, metrics and diagnostics.

## Typical Mistakes

- Do not perform concurrent writes directly against a connection unless the
  connection API serializes them.
- Do not leak bearer tokens in query strings when rendering endpoint diagnostics.
- Do not assume WebSocket reconnection is automatic product behavior; callers own
  policy.
- Do not make message handlers mutate shared state without a strand/serialization
  rule in the caller.

## Tests

`test_fcl_http_websocket` covers WebSocket echo over the HTTP server port and TLS
client connection behavior.
