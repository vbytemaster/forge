# fcl_yamux

`fcl_yamux` implements Yamux multiplexed sessions over a single
`fcl_transport::stream`. It gives higher layers a `transport::session` with
independent logical streams while keeping flow control, reset, close and
resource accounting inside the Yamux owner.

## When To Use

- A secure TCP or libp2p-compatible path needs multiplexed streams.
- P2P direct TCP profiles need a Yamux session over an already authenticated
  stream.
- Tests need a transport-neutral session handle backed by Yamux.

## When Not To Use

- Do not use Yamux as a security layer. TLS/Noise/STCP owns authentication.
- Do not put peer identity, protocol negotiation, DHT, relay or application
  retries here.
- Do not bypass `fcl_transport` stream/session handles with Yamux-specific
  product APIs.

## Public Modules

- `fcl.yamux.options`
- `fcl.yamux.session`
- `fcl.yamux.exceptions`
- `fcl.yamux`

Target: `fcl_yamux`.

Dependencies: `fcl_exceptions`, `fcl_transport`, Boost.Asio.

## Examples

```cpp
import fcl.yamux;

boost::asio::awaitable<void> run_client_mux(fcl::transport::stream base) {
   auto mux = fcl::yamux::session{
      std::move(base),
      fcl::yamux::side::initiator,
      fcl::yamux::options{.max_streams = 1024},
   };

   auto stream = co_await mux.async_open_stream();
   co_await stream.async_write_frame(std::span<const std::uint8_t>{payload});
   co_await mux.async_close();
}
```

```cpp
boost::asio::awaitable<void> serve_mux(fcl::transport::stream base) {
   auto session = fcl::yamux::make_session(
      std::move(base),
      fcl::yamux::side::responder);

   auto stream = co_await session.async_accept_stream();
   auto frame = co_await stream.async_read_frame_chunk();
   co_await stream.async_write_frame(std::move(frame));
}
```

## Boundaries

- `cancel()` is abortive and propagates reset semantics where possible.
- `async_close()` is graceful session shutdown.
- Reset streams are not valid for further read/write operations.
- Lower transport failures are translated to typed Yamux boundary errors.

## Tests

`test_fcl_yamux` covers stream open/accept, flow control, resource limits,
reset validity, cancel propagation, lower transport write failures, clean close
and integration through `fcl_transport::session`.
