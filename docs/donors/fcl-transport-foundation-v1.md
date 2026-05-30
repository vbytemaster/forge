# FCL Transport Foundation v1 Donor Traceability

This is a traceability note, not a roadmap. The canonical network block order
stays in `docs/network/quic-p2p.md`.

## Goal

`fcl_transport` is the reusable low-level substrate for direct transports,
muxers and future API-over-transport code. It owns byte streams, multiplexed
sessions, frame helpers, limits, connector/listener contracts and registry
selection. It must not own Peer ID, multiaddr, libp2p security, Yamux policy,
Relay, DHT, GossipSub or API frame semantics.

## Donor Sources

| Donor | Files inspected | Accepted patterns |
| --- | --- | --- |
| go-libp2p transport core | `donors/go-libp2p/core/transport/transport.go` | Separate dial/listen contracts; listener returns accepted upgraded connection; unsupported transport is explicit; concrete transports remain behind a transport interface. |
| go-libp2p transport testsuite | `donors/go-libp2p/p2p/transport/testsuite/transport_suite.go` | Test connector/listener behavior through common transport contracts rather than concrete TCP/QUIC internals. |
| rust-libp2p transport core | `donors/rust-libp2p/core/src/transport.rs` | Transport output is abstract; dialing and listening are separate; unsupported address/protocol is a typed transport error. |
| rust-libp2p boxed/choice/dummy/memory transports | `donors/rust-libp2p/core/src/transport/{boxed,choice,dummy,memory}.rs` | Type-erasure and registry/choice style composition are valid, but FCL keeps C++/Boost-shaped owner types. |
| libp2p connection specs | `donors/libp2p-specs/connections/README.md` | Upgrade/security/mux layers sit above raw transport; `fcl_transport` must stay below those layers. |

## FCL Decisions

- `stream_connector` and `stream_listener` model raw byte-stream transports such
  as future TCP/STCP.
- `session_connector` and `session_listener` model native multiplexed transports
  such as QUIC.
- `registry` routes by `transport::endpoint::protocol_kind`, not by multiaddr or
  peer identity.
- Missing transport support throws `fcl::transport::exceptions::unsupported_protocol`.
- Duplicate registration throws `fcl::transport::exceptions::duplicate_registration`.
- Builders are intentionally out of scope for this block.

## FCL Tests

- `test_fcl_transport transport_stream_delegates_and_preserves_buffered_frames`
- `test_fcl_transport transport_session_delegates_open_accept_close_cancel`
- `test_fcl_transport transport_frame_handles_partial_multiple_and_limit`
- `test_fcl_transport transport_connector_listener_wrappers_preserve_endpoints`
- `test_fcl_transport transport_registry_routes_and_rejects_missing_or_duplicate`
