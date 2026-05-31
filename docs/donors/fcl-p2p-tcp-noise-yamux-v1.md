# FCL P2P TCP + Noise + Yamux Traceability

This note is proof traceability for Block E.2a. It is not a second roadmap; the
canonical block order remains `docs/network/quic-p2p.md`.

## Scope

Supported in this slice:

- direct `/tcp/.../p2p/<peer>` dial/listen;
- libp2p multistream-select security negotiation for Noise;
- libp2p Noise Peer ID verification and identity payload handling;
- `/yamux/1.0.0` muxer selection;
- reusable `fcl_yamux` session as the resulting `transport::session`;
- FCL <-> go-libp2p and FCL <-> rust-libp2p Ping, Identify and framed echo.

Deferred:

- the TLS security branch for libp2p TCP;
- WebSocket transport paths `/ws` and `/wss`;
- product/API integration above P2P.

## Donors Inspected

| Area | Donor files | Accepted pattern |
| --- | --- | --- |
| TCP transport layering | `donors/go-libp2p/core/transport/transport.go`, `donors/go-libp2p/p2p/transport/tcp/tcp.go`, `donors/rust-libp2p/transports/tcp/src/provider/tokio.rs` | TCP is a raw transport below security and mux upgrade. |
| Upgrade pipeline | `donors/go-libp2p/p2p/net/upgrader/upgrader.go`, `donors/go-libp2p/p2p/net/upgrader/conn.go`, `donors/rust-libp2p/libp2p/src/builder.rs` | Transport -> security -> mux -> swarm/session layering. |
| Security negotiation | `donors/go-libp2p/p2p/test/negotiation/security_test.go`, `donors/go-libp2p/p2p/security/noise/handshake.go`, `donors/rust-libp2p/interop-tests/src/arch.rs` | Noise is negotiated as the TCP security protocol and verifies the remote peer key. |
| Yamux negotiation | `donors/go-libp2p/p2p/test/negotiation/muxer_test.go`, `donors/go-libp2p/p2p/muxer/yamux`, `donors/rust-libp2p/muxers/yamux` | `/yamux/1.0.0` is the TCP stream multiplexer. |
| Live acceptance | `donors/go-libp2p/test-plans/cmd/ping/main.go`, `donors/rust-libp2p/examples/ping/src/main.rs`, `donors/rust-libp2p/interop-tests/src/arch.rs` | Both Go and Rust expose TCP+Noise+Yamux as normal libp2p direct transport composition. |

## FCL Coverage

| Behavior | FCL component test | Live interop scenario |
| --- | --- | --- |
| FCL TCP listener/dialer establishes direct P2P session through Noise and Yamux | `test_fcl_quic_p2p p2p_direct_tcp_nodes_negotiate_noise_yamux_and_echo_frames` | FCL listener <-> Go/Rust dialer `tcp ping`, `tcp identify`, `tcp echo`; Go/Rust listener <-> FCL dialer same scenarios |
| Noise peer mismatch is rejected as typed P2P failure | `test_fcl_quic_p2p p2p_direct_tcp_rejects_noise_peer_mismatch` | Component-level only; live fixtures use valid libp2p identities |
| WebSocket paths remain outside the support claim | `test_fcl_quic_p2p p2p_websocket_multiaddr_is_parseable_but_not_dialable` | No live scenario |
| Relay/DCUtR continue to use reusable Yamux without changing wire behavior | Existing relay/DCUtR component and live interop scenarios | Existing Relay v2, relayed stream and DCUtR live matrix |

## Notes

- `fcl_tcp` remains raw TCP and does not know Peer ID, Noise, multistream-select
  or Yamux.
- `fcl_yamux` remains reusable mux mechanics and does not know P2P identity or
  protocol negotiation.
- `fcl_p2p` owns the libp2p-specific security payload, Peer ID verification,
  multistream-select protocol choice and direct path policy.
