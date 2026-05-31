# FCL STCP v1 Donor Traceability

This is a traceability note, not a roadmap. The canonical network block order
stays in `docs/network/quic-p2p.md`.

## Goal

`fcl_stcp` provides reusable TCP+TLS mechanics over `fcl_tcp` and
`fcl_transport`. It keeps TLS socket handshakes, certificate verification,
fingerprint checks, mTLS and ALPN below P2P/API layers, while leaving Peer ID,
security protocol choice, multistream-select, Yamux and API frames out of scope.

## Donor Sources

| Donor | Files inspected | Accepted patterns |
| --- | --- | --- |
| Boost.Asio SSL examples | `spring/libraries/boost/libs/asio/example/cpp11/ssl/{client.cpp,server.cpp}`, `spring/libraries/boost/libs/asio/example/cpp20/coroutines/*` | Use `ssl::stream<tcp::socket>`, coroutine handshakes and explicit close/cancel mechanics. |
| OpenSSL APIs | OpenSSL 3 certificate, fingerprint and ALPN APIs | Use in-memory PEM loading, certificate DER extraction and ALPN callbacks without shelling out to `openssl`. |
| libp2p TLS specs/donors | `donors/libp2p-specs/tls/tls.md`, `donors/go-libp2p/p2p/security/tls/{transport.go,conn.go,extension.go}`, `donors/rust-libp2p/transports/tls/src/{upgrade.rs,certificate.rs,verifier.rs}` | Keep libp2p identity verification above STCP; STCP owns TLS mechanics only. |
| `fcl_tcp` donor pass | `docs/donors/fcl-tcp-transport-v1.md` | Preserve raw TCP upgrade surface so TLS can consume the native socket before `transport::stream` wrapping. |

## FCL Decisions

- `tcp::connection` can be used as raw TCP or released for TLS upgrade.
- `stcp::connection` exposes raw secure bytes and can be converted into
  `transport::stream_connection`.
- Client/server options carry TLS trust, fingerprint, optional certificate/key
  PEM and ALPN protocols.
- `fcl_stcp` does not register in `transport::registry` yet because direct
  routing is still by TCP endpoint protocol; upper layers choose whether to
  upgrade raw TCP.
- Close is deterministic transport teardown: it closes the underlying TCP
  socket instead of waiting indefinitely for a peer TLS close-notify.

## FCL Tests

- `test_fcl_tcp tcp_connection_supports_native_handoff`
- `test_fcl_stcp stcp_loopback_roundtrip_and_transport_stream`
- `test_fcl_stcp stcp_upgrades_existing_tcp_connection`
- `test_fcl_stcp stcp_rejects_wrong_hostname_and_fingerprint`
- `test_fcl_stcp stcp_supports_mutual_tls`
