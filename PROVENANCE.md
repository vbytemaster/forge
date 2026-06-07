# FCL Provenance

This file records source lineage and donor boundaries for FCL. It is not a
license by itself; see `LICENSE`, `NOTICE`, and `THIRD_PARTY_LICENSES`.

Audit date: 2026-06-07.

## Confirmed Derived Or Adapted Source

The initial standalone import (`02ef01e Initial standalone storlane-fc import`)
contained FC-style code under `include/fc` and `src`. That import carried the
MIT notice for AntelopeIO/spring, EOS Network Foundation, and EOSIO/eos. Later
commits moved and rewrote portions into FCL modules. The following current
areas retain file-level source lineage or substantial layout/semantic
continuity:

- `libraries/raw`: raw serialization, datastream, varint, enum/raw variant
  support.
- `libraries/variant`: variant value/object/static-variant support and legacy
  serialization behavior.
- `libraries/core`: selected utility code including `uint128`, string helpers,
  UTF-8 helpers, type names, time/version helpers where the file history traces
  to the initial FC import.
- `libraries/crypto`: selected FC/EOS-compatible primitives, encodings, digest
  wrappers, key/signature compatibility helpers, and BLS/BN/secp256k1 adapter
  code where file history traces to the initial import or vendored code.
- `libraries/log`: selected logger/message/config code tracing to FC logging.
- `libraries/json`: selected JSON codec behavior tracing to FC JSON code, even
  where the backend and public API were later hardened.

FCL modifications and original additions around these portions are distributed
under Apache-2.0. Upstream MIT attribution and license compatibility notices
are preserved in `THIRD_PARTY_LICENSES`.

## Original FCL Source In This Audit

The following areas were introduced during FCL foundation hardening or later
network/API work and did not show source continuity to the initial FC import in
this audit:

- `libraries/app`
- `libraries/config`
- `libraries/program_options`
- `libraries/yaml`
- current Boost.Describe-based `libraries/reflect`
- `libraries/asio`, `libraries/env`, `libraries/exceptions`, `libraries/http`,
  `libraries/schema`, `libraries/transport`, `libraries/websocket`
- `libraries/quic`, `libraries/tcp`, `libraries/stcp`, `libraries/yamux`,
  `libraries/p2p`
- `libraries/api`, `libraries/api_transport`, `libraries/plugins`,
  `libraries/tui`

If a future file-level audit proves source continuity for a specific file in
one of these areas, update this section and `THIRD_PARTY_LICENSES`.

## Compatibility Donors Only

Some donor projects are used as behavior, protocol, or acceptance-test
references. They do not create runtime source attribution unless source text,
schemas, generated code, or vendored runtime code are copied into FCL.

- libp2p specifications, go-libp2p, go-libp2p-kad-dht, go-libp2p-pubsub, and
  rust-libp2p are compatibility donors for `fcl_p2p`, Yamux, QUIC/TCP
  transport composition, Relay, DCUtR, AutoNAT, DHT, Rendezvous, and GossipSub.
  The 2026-06-07 audit found no copied `.proto` files, protobuf-generated
  files, or copied Go/Rust libp2p source under `libraries/p2p` or
  `tests/quic_p2p`. FCL P2P codecs are hand-written C++ implementations of the
  wire behavior.
- Boost.Asio and Boost.Beast are architecture and mechanics donors for async
  composed operations, buffering, and HTTP/WebSocket behavior. They are normal
  build dependencies, not copied source in FCL.
- Go and Rust libp2p fixtures under `tests/libp2p_interop` import donor
  dependencies for live compatibility tests. They are development/test fixture
  dependencies and do not make libp2p a runtime source component of FCL.

If future work copies libp2p `.proto` schemas, generated code, or source text,
that work must either be rewritten as an independent implementation or add the
exact upstream license and attribution to `THIRD_PARTY_LICENSES` before merge.

## Embedded And Vendored Third-Party Source

FCL contains or vendors permissively licensed third-party source. Confirmed
items include:

- `vendor/secp256k1`: Bitcoin Core secp256k1, MIT.
- `vendor/bn256`: EOS Network Foundation bn256, MIT.
- `vendor/bls12-381`: Matthias Schonebeck BLS12-381, MIT.
- `vendor/utf8cpp`: Nemanja Trifunovic UTF8-CPP, permissive license.
- `libraries/crypto/city.cpp` and `libraries/crypto/include/fcl/crypto/city.cppm`:
  Google CityHash, MIT.
- `libraries/crypto/base58.cpp`: Satoshi Nakamoto and The Bitcoin Developers,
  MIT/X11.
- `libraries/crypto/include/fcl/crypto/base64.cppm`: Rene Nyffenegger base64
  implementation with Kevin Heifner modification notice.
- `libraries/core/uint128.cpp`: portions adapted from Evan Teran.

Submodules may contain their own third-party dependency notices, such as
Catch2 under `vendor/bn256/third-party`. Those notices remain with the vendored
source and are not collapsed into FCL authorship.
