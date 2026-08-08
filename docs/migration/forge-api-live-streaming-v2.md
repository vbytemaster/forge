# Migrate To Forge API Live Streaming v2

Forge API wire v2 replaces the experimental batch-shaped streaming surface.
Unary local and HTTP calls keep their C++ method shape. API-over-stream peers
must be upgraded together.

## C++ Streaming Methods

Replace vector-shaped methods:

```cpp
awaitable<std::vector<chunk>> fetch(fetch_request);
awaitable<put_result> put(std::vector<chunk>);
awaitable<std::vector<response>> exchange(std::vector<request>);
```

with live endpoint methods:

```cpp
awaitable<void> fetch(fetch_request, stream_writer<chunk>);
awaitable<put_result> put(put_request, stream_reader<chunk>);
awaitable<void> exchange(session_request, duplex_stream<request, response>);
```

Use `handle<Interface>::async_open<Method>()` on the caller side. Consume or
produce items incrementally, close the sending direction explicitly, and await
`async_finish()` for the terminal result.

`FORGE_API_METHOD` infers the method kind from the final endpoint. An ordinary
`std::vector<T>` remains an ordinary unary DTO. Overloaded methods use
`FORGE_API_METHOD_EXACT` with an exact member pointer.

## Network Migration

- Upgrade both endpoints before enabling v2 traffic.
- Change the default P2P API protocol from `/forge/api/1` to `/forge/api/2`.
- Change the resolver service default from `/forge/api/resolver/1` to
  `/forge/api/resolver/2`.
- Custom protocol IDs remain application-owned, but both peers must implement
  the mandatory v2 session hello and credit frames.
- Old and new P2P peers fail protocol selection or the session handshake
  explicitly; there is no legacy decoder or vector fallback.

The libp2p transport, peer IDs, security negotiation, multiplexers, DHT and
resolver selection model are unchanged. Only the Forge API application
protocol carried by the selected stream changes.

## HTTP And WebSocket

- HTTP/1.1 server and client streams use versioned length-delimited API bodies.
- HTTP/1.1 bidirectional methods are unsupported; use WebSocket, P2P or QUIC.
- WebSocket peers must both use the v2 session handshake. Every WebSocket
  message is binary and contains exactly one length-delimited Forge transport
  frame. Text messages, a frame split across messages and multiple frames
  coalesced into one message are protocol errors.
- `api_binding::connect(socket)` now returns a live
  `forge::api::websocket::connection`; retain it for the lifetime of calls and
  obtain typed APIs from that connection. It no longer installs a callback and
  returns `void`.
- `api_binding::accept(socket)` now owns the server session loop and completes
  only when that session closes. Spawn or await it as a long-lived connection
  task instead of treating completion as handler installation.
- Existing unary HTTP routes do not use the v2 stream session and retain their
  current request and response mapping.
