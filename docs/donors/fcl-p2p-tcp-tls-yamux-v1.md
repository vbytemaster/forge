# FCL P2P TCP + TLS + Yamux Traceability

This note is proof traceability for Block E.2b. It is not a second roadmap; the
canonical block order remains `docs/network/quic-p2p.md`.

## Scope

Supported in this slice:

- direct `/tcp/.../p2p/<peer>` dial/listen through the libp2p TLS branch;
- multistream-select security negotiation for `/tls/1.0.0`, with `/noise`
  retained as fallback;
- libp2p TLS certificate generation and verification using OID
  `1.3.6.1.4.1.53594.1.1`;
- `SignedKey` verification over `libp2p-tls-handshake:` plus certificate SPKI;
- Peer ID verification from the certificate extension key;
- missing or malformed certificate identity extensions are rejected in P2P
  verification paths;
- ALPN negotiation for `/yamux/1.0.0`, with `libp2p`/empty ALPN falling back
  to post-TLS multistream mux negotiation;
- reusable `fcl_stcp` and `fcl_yamux` mechanics, with P2P identity semantics
  kept private to `fcl_p2p`;
- FCL <-> go-libp2p and FCL <-> rust-libp2p Ping, Identify and framed echo.

Deferred:

- WebSocket transport paths `/ws` and `/wss`;
- product/API integration above P2P;
- non-Ed25519 live identity vectors beyond donor compatibility already covered
  by Go/Rust live TLS fixtures.

## Donors Inspected

| Area | Donor files | Accepted pattern |
| --- | --- | --- |
| TLS spec | `donors/libp2p-specs/tls/tls.md` | TLS 1.3, no SNI for libp2p v1, exactly one certificate, self-signed cert, libp2p public-key extension and Peer ID verification. |
| Inline mux negotiation | `donors/libp2p-specs/connections/inlined-muxer-negotiation.md` | Prefer ALPN mux negotiation and fall back to post-TLS multistream-select when ALPN returns `libp2p` or no muxer. |
| Go TLS | `donors/go-libp2p/p2p/security/tls/{crypto.go,transport.go,transport_test.go}` | `/tls/1.0.0` security transport, extension signing, custom certificate verification and ALPN muxer handoff. |
| Rust TLS | `donors/rust-libp2p/transports/tls/src/{certificate.rs,verifier.rs,upgrade.rs}`, `donors/rust-libp2p/transports/tls/tests/smoke.rs` | Strict certificate extension verification, exact one-cert chain, validity checks and transport upgrade shape. |
| TCP upgrader | `donors/go-libp2p/p2p/net/upgrader/upgrader.go`, `donors/rust-libp2p/libp2p/src/builder.rs` | TCP remains raw below security; security branch upgrades to muxed transport/session. |

## FCL Coverage

| Behavior | FCL component test | Live interop scenario |
| --- | --- | --- |
| FCL TCP listener/dialer establishes direct P2P session through TLS and Yamux | `test_fcl_quic_p2p p2p_direct_tcp_nodes_prefer_tls_yamux_and_echo_frames` | `test_fcl_libp2p_interop` `tcp-tls ping`, `tcp-tls identify`, `tcp-tls echo` for FCL <-> Go/Rust |
| Expected Peer ID mismatch is rejected as typed P2P failure | `test_fcl_quic_p2p p2p_direct_tcp_rejects_tls_peer_mismatch` | Component-level only; live fixtures use valid libp2p identities |
| Missing signed certificate identity extension is rejected | `test_fcl_quic_p2p p2p_certificate_without_libp2p_extension_is_rejected`, `p2p_direct_quic_rejects_missing_certificate_identity_without_expected_peer`, `p2p_direct_quic_rejects_missing_certificate_identity_with_endpoint_peer` | Component-level failure path; live fixtures use valid libp2p identities |
| Generic TLS mechanics enforce SNI policy, full certificate-chain verifier, TLS 1.3 and ALPN selection | `test_fcl_stcp stcp_controls_sni_explicitly`, `stcp_verifier_receives_full_certificate_chain`, `stcp_requires_tls13_by_default`, `stcp_alpn_selects_client_preferred_supported_protocol` | Covered indirectly by TCP TLS live fixtures |
| Noise fallback remains supported | `test_fcl_libp2p_interop` `tcp ping`, `tcp identify`, `tcp echo` for FCL <-> Go/Rust | FCL listener/dialer interop with Go/Rust Noise-only TCP fixtures |
| `/ws` and `/wss` are parse/store only | `test_fcl_quic_p2p p2p_websocket_multiaddr_is_parseable_but_not_dialable` | No live dial/listen scenario |

## Notes

- `fcl_stcp` remains a generic TLS mechanics layer and imports no `fcl_p2p`.
- `fcl_p2p` owns libp2p certificate extension generation, Peer ID verification
  and security/mux protocol selection.
- `node::impl` continues to talk to private direct profiles and does not import
  `fcl_tcp` or `fcl_stcp`.
