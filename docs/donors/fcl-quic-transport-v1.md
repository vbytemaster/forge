# FCL QUIC Transport Alignment v1

This note is traceability for Block D.5. It is not a roadmap source of truth;
the canonical block order remains `docs/network/quic-p2p.md`.

## Donor Inventory

| Source | Files inspected | Accepted pattern |
|---|---|---|
| libp2p specs | `libp2p-specs/quic/README.md`, `libp2p-specs/addressing/README.md` | QUIC v1 is represented as UDP + `/quic-v1`; QUIC is a first-class transport; ALPN is mandatory. |
| go-libp2p | `go-libp2p/p2p/transport/quic/transport.go` | QUIC transport owns dial/listen, endpoint reporting, connection lifecycle and stream opening; P2P identity remains above transport mechanics. |
| rust-libp2p | `rust-libp2p/transports/quic/src/transport.rs`, `tests/smoke.rs` | QUIC transport is a multiplexed transport producing stream-muxer-like sessions; listen/dial are routed through a transport abstraction. |
| ngtcp2 | `ngtcp2/examples/*quictls*` | QUIC TLS and endpoint mechanics remain owned by the QUIC engine layer. |

Local donor root used for inspection:
`/Users/vladimirtarnakin/.openclaw/workspace/Projects/Storlane/donors`.

## FCL Target

- `fcl_quic` exposes native `fcl::transport::session_connector` and
  `fcl::transport::session_listener`.
- `fcl_quic` remains owner of ngtcp2, OpenSSL QUIC TLS mechanics, ALPN,
  certificates, native QUIC streams and transport limits.
- `fcl_quic` does not own Peer ID, multistream-select, Relay, DHT,
  Rendezvous, GossipSub or P2P routing policy.
- `transport::registry` routes `endpoint::protocol_kind::quic_v1` to QUIC
  session connect/listen.

## Supported Behavior Matrix

| Behavior | Donor/spec source | FCL coverage |
|---|---|---|
| QUIC v1 endpoint shape | libp2p QUIC spec, addressing spec | `test_fcl_quic`: endpoint conversion and invalid protocol rejection |
| Native session connect/listen | go-libp2p QUIC transport, rust-libp2p QUIC transport | `test_fcl_quic`: direct connector/listener loopback |
| Registry routing | rust-libp2p `Transport`, FCL transport registry contract | `test_fcl_quic`: registry listen/connect roundtrip |
| ALPN and security options pass-through | libp2p QUIC spec ALPN, ngtcp2 TLS examples | `test_fcl_quic`: custom ALPN + pinned fingerprint connect |
| Limit mapping | go/rust resource/lifecycle separation, FCL transport limits | `test_fcl_quic`: generic transport stream limit override |
| Cancellation/close contract | transport abstraction acceptance criteria | `test_fcl_quic`: connector cancel and listener accept unblock |

## Explicit Non-Goals

- No P2P rebase in this pass.
- No Peer ID or libp2p TLS extension logic in `fcl_quic`.
- No support claim for `/quic` draft-29 as a direct transport; Block B only
  parses/stores `/quic` at the multiaddr layer.
