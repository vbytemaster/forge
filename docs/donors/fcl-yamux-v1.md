# FCL Yamux v1 Donor Traceability

This is a traceability note, not a roadmap. The canonical network block order
stays in `docs/network/quic-p2p.md`.

## Goal

`fcl_yamux` is the reusable multiplexing layer that turns one
`fcl::transport::stream` into a `fcl::transport::session`. It owns Yamux frame
encoding, stream IDs, stream state, flow control, close/reset behavior and
bounded queues. It must not own P2P identity, protocol negotiation, relay,
discovery, pubsub, TCP, STCP or QUIC mechanics.

## Donor Sources

| Donor | Files inspected | Accepted patterns |
| --- | --- | --- |
| libp2p specs | `donors/libp2p-specs/yamux/README.md` | Yamux header layout, DATA/WINDOW_UPDATE/PING/GOAWAY frame types, SYN/ACK/FIN/RST flags, odd/even stream IDs, ping ACK and goaway semantics. |
| go-libp2p Yamux | `donors/go-libp2p/p2p/muxer/yamux/{transport.go,stream.go,transport_test.go}` | Large production stream windows, resource-manager-backed limits, session wrapper shape and testsuite-driven behavior. |
| rust-libp2p Yamux | `donors/rust-libp2p/muxers/yamux/src/lib.rs` | Explicit inbound stream buffer limits, bounded backlog behavior and separation between muxer mechanics and higher protocol negotiation. |
| rust-libp2p compliance | `donors/rust-libp2p/muxers/yamux/tests/compliance.rs` | Close-implies-flush, read-after-close, concurrent stream isolation and transport-session compliance checks. |
| rust-libp2p muxer harness | `donors/rust-libp2p/muxers/test-harness` | Generic muxer acceptance criteria should be tested through paired streams rather than concrete TCP/P2P wiring. |

## FCL Decisions

- Public modules are `fcl.yamux.options`, `fcl.yamux.session`,
  `fcl.yamux.exceptions` and aggregate `fcl.yamux`.
- `session(transport::stream, side, options)` starts the read/demux loop lazily
  from the first async operation.
- `async_open_stream()` allocates odd/even stream IDs according to local side
  and allows early DATA before the remote ACK.
- `async_accept_stream()` returns inbound streams from a bounded pending backlog.
- One session read loop owns all reads from the underlying byte stream.
- All writes are serialized through one frame write path.
- Per-stream DATA writes are split by remote window and `options::max_frame_size`.
- Read-side DATA accounting sends WINDOW_UPDATE only after the consumer reads
  bytes from the substream.
- `async_close()` sends GOAWAY where possible and closes the underlying stream;
  `cancel()` wakes waiters deterministically.
- Options are behavioral: windows, frame size, stream count and buffer limits all
  affect runtime behavior and have tests.

## Supported Behaviors And Tests

| Behavior | Source | FCL coverage |
| --- | --- | --- |
| Open/accept stream and odd/even IDs | libp2p spec, Go testsuite | `test_fcl_yamux yamux_supports_open_accept_and_early_data` |
| Early DATA before ACK | libp2p spec, Go Yamux behavior | `test_fcl_yamux yamux_supports_open_accept_and_early_data` |
| Concurrent streams do not cross-deliver payloads | Rust muxer harness | `test_fcl_yamux yamux_keeps_concurrent_stream_payloads_isolated` |
| Flow control waits for WINDOW_UPDATE | libp2p spec, Go/Rust Yamux | `test_fcl_yamux yamux_applies_flow_control_with_window_updates` |
| Close flushes pending DATA; read after FIN fails typed | Rust compliance | `test_fcl_yamux yamux_close_flushes_and_read_after_close_is_rejected` |
| Frame size split, stream buffer limit and malformed frame rejection | libp2p spec, Rust inbound buffer limit | `test_fcl_yamux yamux_rejects_limits_and_malformed_frames_with_typed_errors` |
| Stream zero misuse and oversized DATA rejection | libp2p spec | `test_fcl_yamux yamux_rejects_limits_and_malformed_frames_with_typed_errors` |
| PING ACK and GOAWAY close | libp2p spec | `test_fcl_yamux yamux_handles_ping_and_goaway_control_frames` |
| `transport::session` wrapper delegation | Rust muxer harness | `test_fcl_yamux yamux_exposes_transport_session_wrapper` |

## Deferred To Block E

- Replacing the private P2P Yamux compatibility code.
- Negotiating the muxer protocol in the P2P stack.
- Live FCL to Go/Rust P2P interop over TCP paths.
