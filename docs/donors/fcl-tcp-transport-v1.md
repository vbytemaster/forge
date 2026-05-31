# FCL TCP Transport v1 Donor Traceability

This is a traceability note, not a roadmap. The canonical network block order
stays in `docs/network/quic-p2p.md`.

## Goal

`fcl_tcp` is the first concrete stream transport over `fcl_transport`. It wraps
Boost.Asio TCP sockets into `transport::stream_connection` and keeps TLS,
Yamux, P2P identity, API frames and multiaddr mapping out of scope.

## Donor Sources

| Donor | Files inspected | Accepted patterns |
| --- | --- | --- |
| Boost.Asio coroutine TCP examples | `spring/libraries/boost/libs/asio/example/cpp20/coroutines/echo_server.cpp`, `refactored_echo_server.cpp` | Use native Asio sockets, acceptors and `boost::asio::awaitable` mechanics directly. |
| bitstore-fc TCP socket | `bitstore-fc/src/network/tcp_socket.cpp` | Socket open/close, endpoint extraction and option setup are useful; legacy `fc::future` style is rejected. |
| bitstore-fc Asio resolver | `bitstore-fc/src/asio.cpp` | Resolver flow confirms DNS belongs in TCP mechanics; legacy promise/future wrapping is rejected. |
| Spring state history plugin | `spring/plugins/state_history_plugin/include/eosio/state_history_plugin/session.hpp`, `spring/plugins/state_history_plugin/state_history_plugin.cpp` | Connection lifecycle should close/cancel cleanly and set TCP options close to socket ownership. |
| libp2p transport donors | `donors/go-libp2p/core/transport`, `donors/go-libp2p/p2p/transport/testsuite`, `donors/rust-libp2p/core/src/transport.rs` | Raw transport should stay below security/mux/P2P layers; FCL keeps C++/Boost-style public contracts. |

## FCL Decisions

- `fcl_tcp` owns only raw TCP stream connect/listen.
- Public async methods return `boost::asio::awaitable`.
- The public contract uses `fcl::transport::endpoint` and returns
  `fcl::transport::stream_connection`.
- DNS is supported for connect, but listen requires concrete `ip4` or `ip6`.
- Port `0` is valid for listen and resolves to the actual bound port.
- TLS-over-TCP, Yamux, P2P and API-over-transport remain later blocks.

## FCL Tests

- `test_fcl_tcp tcp_stream_roundtrip_and_framing`
- `test_fcl_tcp tcp_options_affect_stream_reads`
- `test_fcl_tcp tcp_accept_can_be_canceled_or_closed`
- `test_fcl_tcp tcp_rejects_invalid_endpoints_and_refused_connects`
