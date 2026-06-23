# forge_transport

`forge_transport` is the reusable byte stream/session substrate used by TCP, STCP,
Yamux, QUIC, P2P and API-over-transport bindings. It owns transport-neutral
concept wrappers, endpoint values, frame helpers and pooled byte chunks. It does
not own sockets, peer identity, HTTP, WebSocket, P2P routing or application API
semantics.

## When To Use

- A concrete transport needs to expose a common `stream` or `session`.
- A higher layer needs length-prefixed frame helpers over an existing byte
  stream.
- Hot paths need reusable `chunk` storage without forcing vector roundtrips.
- Tests need fake connectors/listeners/sessions with the same public contract as
  real transports.

## When Not To Use

- Do not add peer IDs, relay policy, protocol negotiation or discovery here.
- Do not put API contracts or RPC method names in this layer. Use
  `forge_transport_api` above it.
- Do not use `buffer_pool` as an unbounded queue or application cache.

## Public Modules

- `forge.transport.buffer`
- `forge.transport.endpoint`
- `forge.transport.frame`
- `forge.transport.stream`
- `forge.transport.session`
- `forge.transport.connector`
- `forge.transport.listener`
- `forge.transport.registry`
- `forge.transport.limits`
- `forge.transport.exceptions`
- `forge.transport`

Target: `forge_transport`.

Dependencies: `forge_exceptions`, Boost.Asio.

## Examples

```cpp
import forge.transport.buffer;
import forge.transport.frame;

auto pool = forge::transport::buffer_pool{
   forge::transport::buffer_pool_options{
      .default_capacity = 64 * 1024,
      .max_cached_buffers = 32,
      .max_cached_bytes = 8 * 1024 * 1024,
   }};

auto builder = pool.acquire(4096);
auto writable = builder.writable();
std::copy(payload.begin(), payload.end(), writable.begin());
auto chunk = builder.commit(payload.size());

std::vector<std::uint8_t> encoded;
forge::transport::encode_frame_to(encoded, chunk.bytes());
auto view = forge::transport::decode_frame_view(encoded);
```

```cpp
import forge.transport.stream;

boost::asio::awaitable<void> echo_frame(forge::transport::stream stream) {
   auto frame = co_await stream.async_read_frame_chunk();
   co_await stream.async_write_frame(std::move(frame));
}
```

## Boundaries

- `stream` and `session` are move-only handles over private concepts.
- Vector APIs remain convenience wrappers; chunk APIs are the fast path.
- Frame decoding supports consumed-offset parsing. Hot paths must not rely on
  repeated front erases of buffered bytes.
- Thread-safety contracts are documented in `docs/runtime/thread-safety.md`.

## Tests

`test_forge_transport` covers chunk lifetime, bounded buffer reuse, frame
encode/decode, decode views without payload allocation, stream/session wrapper
delegation, registry routing and typed error paths.
