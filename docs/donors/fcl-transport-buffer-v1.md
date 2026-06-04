# FCL Transport Buffer v1 Donor Traceability

This is a traceability note, not a roadmap. The canonical network block order
stays in `docs/network/quic-p2p.md`.

## Goal

`fcl_transport` now exposes an additive byte-buffer fast path for serious
stream consumers without breaking the existing vector convenience API. The
buffer layer owns byte chunks, bounded storage reuse and frame parsing helpers;
it does not own TCP, TLS, QUIC, Yamux, P2P, API or content semantics.

## Donor Sources

| Donor | Files inspected | Accepted patterns |
| --- | --- | --- |
| Boost.Beast flat buffer | `/opt/homebrew/include/boost/beast/core/flat_buffer.hpp` | Contiguous readable/writable storage, explicit prepare/commit/consume lifecycle and configured maximum storage size. |
| Boost.Asio buffers | `/opt/homebrew/include/boost/asio/buffer.hpp`, TCP/STCP async read/write usage in FCL | Async operations consume caller-provided buffer sequences; FCL must own write/read storage across suspension. |
| FCL Yamux cleanup | `docs/donors/fcl-yamux-v1.md`, `libraries/yamux/session.cpp` | Consumed-offset parsing and bounded compaction are preferred over front erasing receive buffers. |
| libp2p Yamux donors | `donors/go-libp2p/p2p/muxer/yamux`, `donors/rust-libp2p/muxers/yamux` | Muxer buffers are resource-accounted and higher-level protocol semantics stay out of the byte transport layer. |

## FCL Decisions

- `fcl.transport.buffer` owns `chunk`, `chunk_builder`, `buffer_pool`,
  `buffer_pool_options` and `buffer_pool_stats`.
- Existing `stream::async_read()` and `stream::async_read_frame()` remain stable
  vector-returning convenience APIs.
- New `stream::async_read_chunk()`, `async_read_frame_chunk()`,
  `async_write(chunk)` and `async_write_frame(chunk)` are additive.
- `chunk` is immutable to consumers and carries shared ownership of backing
  bytes. It may represent a subrange of owned storage.
- `chunk_builder` is move-only. `commit(size)` transfers ownership into a
  `chunk`; invalid commit sizes or reuse after commit throw
  `fcl::transport::exceptions::invalid_buffer`.
- `buffer_pool` is bounded by cached buffer count and cached byte capacity.
  Excess returned storage is dropped instead of creating hidden unbounded queues.
- `decode_frame_view(...)` exposes a span into caller-owned bytes and does not
  allocate a payload vector. The old `decode_frame(...)` copies through the view
  for compatibility.
- `stream::async_read_frame_chunk()` uses consumed-offset buffering and bounded
  compaction instead of erasing from the front of the receive buffer.
- TCP and STCP read directly into `chunk_builder` storage. QUIC and Yamux move
  their existing owned read buffers into `chunk` values where safe.
- `fcl::p2p::stream` re-exports the transport buffer layer and adds chunk
  read/write methods as wrappers over the underlying `transport::stream`.
  Existing vector methods remain compatibility conveniences.
- P2P stream pre-read buffering uses the same consumed-offset parsing rule for
  framed reads, so negotiation bytes and trailing framed payloads do not require
  front erasing.

## Supported Behaviors And Tests

| Behavior | Source | FCL coverage |
| --- | --- | --- |
| Bounded reusable buffer storage | Beast dynamic-buffer lifecycle, FCL resource rules | `test_fcl_transport transport_chunk_and_pool_reuse_bounded_storage` |
| Chunk lifetime across async stream wrappers | Asio buffer lifetime rules | `test_fcl_transport transport_stream_delegates_chunk_read_write_without_forcing_vector_path` |
| Frame decode view without payload allocation | Beast contiguous readable storage | `test_fcl_transport transport_frame_view_decodes_without_payload_copy` |
| Framed chunk read preserves trailing bytes without extra reads | FCL Yamux consumed-offset cleanup | `test_fcl_transport transport_stream_reads_framed_chunks_and_preserves_trailing_bytes_without_extra_reads` |
| TCP chunk and framed chunk path | Boost.Asio TCP stream mechanics | `test_fcl_tcp tcp_loopback_roundtrip_and_frames` |
| STCP chunk and framed chunk path | Boost.Asio SSL stream mechanics | `test_fcl_stcp stcp_loopback_roundtrip_and_transport_stream` |
| QUIC transport session chunk path | FCL QUIC transport session adapter | `test_fcl_quic loopback_session_connector_listener_transfer_frames` |
| Yamux muxed stream chunk path | libp2p Yamux buffer/resource donor behavior | `test_fcl_yamux yamux_supports_open_accept_and_early_data` |
| P2P stream chunk wrapper path | Additive transport buffer API, FCL P2P pre-read negotiation needs | `test_fcl_quic_p2p p2p_stream_delegates_chunk_read_write_and_preserves_framed_trailing_bytes` |
| P2P framed pre-read buffer keeps trailing bytes | Beast/FCL consumed-offset parser rule | `test_fcl_quic_p2p p2p_stream_with_buffer_preserves_prefetched_framed_chunks` |
| Relay pumps avoid vector roundtrip when forwarding bytes | Asio buffer lifetime ownership and transport chunk semantics | Existing relay component tests plus chunk forwarding in `node::impl::relay_pump_loop` |

## Non-Goals

- This block does not implement kernel zero-copy, `sendfile`, file-backed
  chunks, content addressing or `contentd` bulk-transfer APIs.
- It does not require P2P or API consumers to migrate off vector convenience
  APIs immediately.
- It does not add protocol-specific imports to `fcl_transport`.
