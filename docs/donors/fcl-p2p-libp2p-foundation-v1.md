# FCL P2P libp2p Foundation v1 Donor Traceability

## Scope

This pass implements the first compatibility foundation for `fcl_p2p`:

- byte-first base58/base32 APIs in `fcl_crypto`;
- `fcl_multiformats` for varint, multicodec, multihash, multibase and address encoding;
- libp2p-style Peer ID and public key Protocol Buffers encoding;
- FCL-style `fcl::p2p::endpoint` that reads/writes libp2p address text;
- QUIC profile helper with ALPN `libp2p`.

`fcl_raw` remains DTO serialization. `fcl_multiformats` is protocol self-description and must not become a DTO codec.

## Donors Inspected

- `donors/libp2p-specs/peer-ids/peer-ids.md`: public/private key Protocol Buffers shape, deterministic encoding rules, identity-vs-sha2 Peer ID rule and key-family codes.
- `donors/libp2p-specs/RFC/0001-text-peerid-cid.md`: legacy base58 Peer ID text and CIDv1 `libp2p-key` base32 representation.
- `donors/libp2p-specs/addressing/README.md`: `/p2p` address component and historical `/ipfs` alias.
- `donors/libp2p-specs/quic/README.md`: `/quic-v1` address component, ALPN `libp2p` and native QUIC streams.
- `donors/go-libp2p` and `donors/rust-libp2p`: donor test criteria source for future live interop harnesses.

## Accepted Patterns

- Use Peer ID canonical bytes as multihash bytes.
- Use identity multihash for small deterministic public key Protocol Buffers payloads and `sha2-256` for larger public keys.
- Preserve legacy Peer ID string as base58btc without multibase prefix.
- Support CIDv1 text form through multibase base32 and multicodec `libp2p-key`.
- Keep FCL public naming as `endpoint`, `protocol_id`, `session`, `stream` rather than exposing `multiaddr` as the main user-facing type.

## Rejected Patterns

- No direct dependency on Go/Rust libp2p runtime code.
- No duplicate base58/base32 implementation inside `fcl_multiformats`; encoders are owned by `fcl_crypto`.
- No `fcl_api::frame` requirement at the QUIC stream layer. API frames remain a higher-level mounted protocol.
- No application-layer shortcuts for Identify, discovery, relay selection or gossip in this block.

## Tests Added

- `test_fcl_crypto`: base58 byte API, legacy wrappers, RFC4648 base32 vectors, uppercase/padding decode and invalid input.
- `test_fcl_multiformats`: varint minimal encoding, multicodec constants, multihash identity/sha2, multibase prefixes and address parse/binary roundtrip.
- `test_fcl_quic_p2p`: Peer ID from libp2p key vectors, legacy/CID text roundtrip, P2P endpoint parse/format and QUIC profile ALPN.

## Historical Scope Boundaries

- This foundation pass did not itself add the live go-libp2p/rust-libp2p
  interop harness. Current live proof is tracked in
  `tests/libp2p_interop/donor_cases.json`.
- Multistream-select, Ping, Identify, peer/path store, AutoNAT, relay, DHT and
  pubsub are covered by later donor notes. This document should not be used as
  the current support-claim surface for those protocols.
- QUIC certificate-based libp2p peer authentication was only a profile direction
  in this pass. Current strict certificate identity proof is tracked in the
  TCP TLS and QUIC/P2P donor matrix entries.
