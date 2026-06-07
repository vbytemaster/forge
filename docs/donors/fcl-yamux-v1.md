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
- Session state is internally synchronized and frame writes are FIFO
  serialized; the project-wide thread safety categories are recorded in
  `docs/runtime/thread-safety.md`.
- Per-stream DATA writes are split by remote window and `options::max_frame_size`.
- A non-zero `WINDOW_UPDATE|SYN` length is treated as the peer-advertised
  initial receive credit. A zero-length SYN uses the local initial-window
  compatibility fallback, so Go-style zero-length SYN opens still allow
  immediate response writes.
- Read-side DATA accounting sends WINDOW_UPDATE only after the consumer reads
  bytes from the substream.
- `async_close()` sends GOAWAY where possible and closes the underlying stream;
  `cancel()` wakes waiters deterministically.
- Substream `cancel()` is abortive: it marks the local stream reset and sends a
  best-effort peer-visible `RST`. Graceful substream close remains
  `async_close()`.
- Options are behavioral: windows, frame size, stream count and buffer limits all
  affect runtime behavior and have tests.
- Window and buffer options are validated together: FCL rejects any configuration
  that would advertise an initial receive window larger than the configured
  per-stream or session receive buffer.
- Stream-attributable receive-buffer abuse resets only the offending stream. The
  session keeps demultiplexing, unrelated streams remain usable and local users
  of the reset stream observe `fcl::yamux::exceptions::stream_reset`.
- Malformed frames, stream-zero misuse, invalid stream ID parity, duplicate
  stream IDs, oversized DATA and GOAWAY/error paths remain session-level
  failures.
- The frame parser keeps a consumed offset and bounded compaction instead of
  erasing from the front of the receive buffer for every frame.
- Yamux owns no internal deadline policy. Callers wrap operations in P2P/API or
  transport-level deadlines when they need time bounds.

## Supported Behaviors And Tests

| Behavior | Source | FCL coverage |
| --- | --- | --- |
| Open/accept stream and odd/even IDs | libp2p spec, Go testsuite | `test_fcl_yamux yamux_supports_open_accept_and_early_data` |
| Early DATA before ACK | libp2p spec, Go Yamux behavior | `test_fcl_yamux yamux_supports_open_accept_and_early_data` |
| Concurrent streams do not cross-deliver payloads | Rust muxer harness | `test_fcl_yamux yamux_keeps_concurrent_stream_payloads_isolated` |
| Flow control waits for WINDOW_UPDATE | libp2p spec, Go/Rust Yamux | `test_fcl_yamux yamux_applies_flow_control_with_window_updates` |
| Non-zero `WINDOW_UPDATE|SYN` sets initial send credit; zero-length SYN keeps compatibility fallback | libp2p spec, Go Yamux behavior | `test_fcl_yamux yamux_enforces_configured_runtime_limits`, `test_fcl_yamux yamux_rejects_limits_and_malformed_frames_with_typed_errors` |
| Close flushes pending DATA; read after FIN fails typed | Rust compliance | `test_fcl_yamux yamux_close_flushes_and_read_after_close_is_rejected` |
| Frame size split, stream buffer limit and malformed frame rejection | libp2p spec, Rust inbound buffer limit | `test_fcl_yamux yamux_rejects_limits_and_malformed_frames_with_typed_errors` |
| Stream count, pending accept backlog, session buffer and max stream window limits | Go resource-manager-backed limits, Rust inbound backlog behavior | `test_fcl_yamux yamux_enforces_configured_runtime_limits` |
| Invalid window/buffer option combinations rejected up front | Rust inbound buffer limits, FCL receive-credit invariant | `test_fcl_yamux yamux_rejects_limits_and_malformed_frames_with_typed_errors` |
| Stream-local buffer overflow resets only the offending stream | Go testsuite reset behavior, Rust inbound buffer limit | `test_fcl_yamux yamux_resets_only_streams_that_exceed_buffers` |
| Attributable session-buffer overflow resets the incoming offending stream | Go resource-manager-backed limits, Rust bounded receive buffers | `test_fcl_yamux yamux_resets_only_streams_that_exceed_buffers` |
| Partial frames, multiple buffered frames and trailing buffered bytes parse without front erase | libp2p spec frame layout, Rust muxer harness buffering expectations | `test_fcl_yamux yamux_parser_preserves_partial_and_buffered_frames` |
| Terminal stream reclamation releases stream-count and buffer budget while preserving typed reset for existing handles | Go reset behavior, Rust stream lifecycle and bounded buffers | `test_fcl_yamux yamux_reclaims_terminal_streams_before_stream_cap`, `test_fcl_yamux yamux_reset_reclaim_releases_buffer_budget` |
| Reset streams become invalid but read/write still fail with typed Yamux reset | Go/Rust reset semantics, FCL transport boundary rule | `test_fcl_yamux yamux_reset_invalidates_stream_and_cancel_sends_rst` |
| Underlying write failure is normalized at the Yamux boundary | FCL typed exception boundary | `test_fcl_yamux yamux_write_failure_throws_typed_yamux_closed` |
| Stream zero misuse and oversized DATA rejection | libp2p spec | `test_fcl_yamux yamux_rejects_limits_and_malformed_frames_with_typed_errors` |
| PING ACK and GOAWAY close | libp2p spec | `test_fcl_yamux yamux_handles_ping_and_goaway_control_frames` |
| `transport::session` wrapper delegation | Rust muxer harness | `test_fcl_yamux yamux_exposes_transport_session_wrapper` |

## P2P Adoption

- Block E.1 replaces the private P2P Yamux compatibility code with this reusable
  layer for Relay/DCUtR.
- TCP-path muxer negotiation remains future P2P work.
- Live FCL to Go/Rust P2P interop over TCP paths remains future work.
