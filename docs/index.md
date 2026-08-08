# FORGE Docs Index

This index points at current FORGE-owned documentation. FORGE is a neutral C++23
infrastructure framework and constructor substrate for distributed services,
DePIN systems, blockchain/control-plane runtimes and plugin-based daemons. It is
not a product layer and does not own downstream storage, billing,
authorization, Spring or Storlane semantics.

Per-library `README.md` files are the quick start and API guide for one library.
Documents below explain cross-library architecture decisions.

## Main Documents

| Document | Purpose |
| --- | --- |
| [releases/8.21.0.md](releases/8.21.0.md) | Forge 8.21.0 verified Chain API, Experimental authenticated state and HTTP deadline fixes. |
| [releases/8.20.0.md](releases/8.20.0.md) | Forge 8.20.0 native guest Contract SDK projects and manifest schema v3 migration. |
| [releases/8.19.0.md](releases/8.19.0.md) | Forge 8.19.0 exact typed JSON and complete Chain Protocol host serialization. |
| [releases/8.18.0.md](releases/8.18.0.md) | Forge 8.18.0 installed Contract Testing component and contract-library workflow. |
| [releases/8.17.0.md](releases/8.17.0.md) | Forge 8.17.0 DB Store durability status and API 2.0 migration. |
| [releases/8.16.1.md](releases/8.16.1.md) | Forge 8.16.1 P2P identity reuse and concurrent handshake safety. |
| [releases/8.16.0.md](releases/8.16.0.md) | Forge 8.16.0 dual-target Contract SDK, digest-lifetime and P2P lifecycle changes. |
| [releases/8.15.0.md](releases/8.15.0.md) | Forge 8.15.0 purpose-scoped BLS signer API for downstream finality services. |
| [releases/8.14.0.md](releases/8.14.0.md) | Forge 8.14.0 bounded Savanna validation, vote accumulation and finalizer safety. |
| [releases/8.13.0.md](releases/8.13.0.md) | Forge 8.13.0 neutral Chain quorum, fork and Savanna primitives. |
| [releases/8.11.0.md](releases/8.11.0.md) | Forge 8.11.0 canonical producer schedule module ownership. |
| [releases/8.12.0.md](releases/8.12.0.md) | Forge 8.12.0 Crypto leaf-family migration. |
| [releases/8.10.0.md](releases/8.10.0.md) | Forge 8.10.0 Contract SDK, EOSIO compatibility and migration guarantees. |
| [releases/8.9.0.md](releases/8.9.0.md) | Forge 8.9.0 Crypto Signer binary API migration and compatibility guarantees. |
| [releases/8.8.0.md](releases/8.8.0.md) | Forge 8.8.0 DB IDs family migration and compatibility guarantees. |
| [releases/8.3.0.md](releases/8.3.0.md) | Forge 8.3.0 changes, Preview API status and source migration notes. |
| [roadmap.md](roadmap.md) | Post-1.0 direction, architecture gates and framework boundaries. |
| [runtime/asio-app.md](runtime/asio-app.md) | Runtime ownership, bounded scheduler, plugin lifecycle and rollback. |
| [runtime/compute-and-snapshots.md](runtime/compute-and-snapshots.md) | Separate CPU execution domain, snapshot readers with an ordered writer, and the boundary for future speculative execution. |
| [web/http-websocket.md](web/http-websocket.md) | HTTP/WebSocket layering, routing, upgrade, retry and backpressure rules. |
| [web/http-fastapi-style-parameters.md](web/http-fastapi-style-parameters.md) | FastAPI-style HTTP parameter binding for multi-argument FORGE API methods. |
| [web/http-files-and-s3.md](web/http-files-and-s3.md) | HTTP file upload/download gaps, Beast capabilities and S3-ready substrate plan. |
| [iterations/forge-xml-http-api-codec-v1.md](iterations/forge-xml-http-api-codec-v1.md) | XML codec and HTTP API multi-codec implementation order for S3-ready typed APIs. |
| [iterations/forge-object-database-v1.md](iterations/forge-object-database-v1.md) | Problem notes for a neutral object database layer based on blockchain and mountd donors. |
| [iterations/forge-db-revisions-v1.md](iterations/forge-db-revisions-v1.md) | Accepted durable DB Revision architecture, typed system tables, atomic capture, revert and pruning invariants. |
| [iterations/forge-db-revisions-migrations-v1.md](iterations/forge-db-revisions-migrations-v1.md) | Future schema migration boundary and its optional use of DB Revision and savepoints. |
| [iterations/forge-db-savepoints-v1.md](iterations/forge-db-savepoints-v1.md) | Accepted DB Core savepoint semantics, backend mapping and Object/Blob/revision participant invariants. |
| [iterations/forge-db-state-services-v1.md](iterations/forge-db-state-services-v1.md) | Scope decisions for shared read views, physical checkpoints, deferred migrations and DB Store revision integration. |
| [security/authenticated-state-production-gate.md](security/authenticated-state-production-gate.md) | Mandatory independent review gate before production activation of authenticated state roots. |
| [iterations/forge-db-mdbx-v1.md](iterations/forge-db-mdbx-v1.md) | Production design for a libmdbx DB Core backend, including thread affinity, snapshot cloning, durability, geometry and parity requirements. |
| [iterations/forge-contract-sdk-toolchain-v1.md](iterations/forge-contract-sdk-toolchain-v1.md) | Accepted baseline for the wasm32 contract SDK, vanilla Clang toolchain, legacy EOSIO compatibility and modern C++23 contract surface. |
| [donors/forge-contract-dual-target-graph-v1.md](donors/forge-contract-dual-target-graph-v1.md) | Bazel, Cargo, CDT and CMake donor boundaries for immutable dual-target contract-library descriptors. |
| [iterations/forge-chain-remote-state-v1.md](iterations/forge-chain-remote-state-v1.md) | Accepted direction for transport-neutral typed contract-state reads and chain transaction submission over Forge API. |
| [donors/forge-chain-remote-state-v1.md](donors/forge-chain-remote-state-v1.md) | Spring/CDT donor boundaries for guest tables, remote state reads and transaction submission. |
| [iterations/forge-net-family-restructure-v1.md](iterations/forge-net-family-restructure-v1.md) | Future direction for grouping network libraries under `forge::net::*`. |
| [network/quic-p2p.md](network/quic-p2p.md) | QUIC transport, P2P peer identity, protocol streams and failure model. |
| [tui/notcurses-component-library.md](tui/notcurses-component-library.md) | TUI value models, deterministic rendering, navigation and Notcurses boundary. |
| [codecs/json-yaml-glaze.md](codecs/json-yaml-glaze.md) | JSON/YAML API shape, Glaze backend isolation and diagnostics. |
| [config/schema-config-program-options.md](config/schema-config-program-options.md) | Schema, config documents, env/CLI adapters, merge order and redaction. |
| [iterations/fcl-secret-provider-v1.md](iterations/fcl-secret-provider-v1.md) | Local secret provider plugin: source loading, redaction, purpose-gated crypto operations and donor baseline. |
| [donors/forge-crypto-signer-binary-v1.md](donors/forge-crypto-signer-binary-v1.md) | BitShares/Graphene donor evidence for typed binary signer results. |
| [forge_concept_ru.md](forge_concept_ru.md) | Original Russian concept and long-form design motivation. |

## Blueprints

Blueprints are planning documents for possible future FORGE directions. They are
not current public API commitments.

| Blueprint | Purpose |
| --- | --- |
| [blockchain-constructor](blueprints/blockchain-constructor/README.md) | Planning map for neutral FORGE building blocks that can support blockchain and DePIN construction without turning FORGE into a product runtime. |

## Library Guides

Each library guide must be useful without reading source first:

- [core](../libraries/core/README.md)
- [exception](../libraries/exceptions/README.md)
- [reflect](../libraries/reflect/README.md)
- [variant](../libraries/variant/README.md)
- [raw](../libraries/raw/README.md)
- [db_ids](../libraries/db/ids/README.md)
- [compression](../libraries/compression/README.md)
- [chain](../libraries/chain/README.md)
- [chain_api](../libraries/chain/api/README.md)
- [db_core](../libraries/db/core/README.md)
- [db_object](../libraries/db/object/README.md)
- [db_blob](../libraries/db/blob/README.md)
- [db_revision](../libraries/db/revision/README.md)
- [db_authenticated](../libraries/db/authenticated/README.md)
- [db_rocksdb](../libraries/db/rocksdb/README.md)
- [json](../libraries/codec/json/README.md)
- [yaml](../libraries/codec/yaml/README.md)
- [xml](../libraries/codec/xml/README.md)
- [schema](../libraries/schema/README.md)
- [config](../libraries/config/core/README.md)
- [program_options](../libraries/config/program_options/README.md)
- [env](../libraries/config/env/README.md)
- [api_core](../libraries/api/core/README.md)
- [api_http](../libraries/api/http/README.md)
- [api_stream](../libraries/api/stream/README.md)
- [api_transport](../libraries/api/transport/README.md)
- [api_quic](../libraries/api/quic/README.md)
- [api_websocket](../libraries/api/websocket/README.md)
- [api_p2p](../libraries/api/p2p/README.md)
- [crypto](../libraries/crypto/README.md)
- [log](../libraries/log/README.md)
- [otlp](../libraries/otlp/README.md)
- [asio](../libraries/asio/README.md)
- [app](../libraries/app/README.md)
- [net/http](../libraries/net/http/README.md)
- [net/websocket](../libraries/net/websocket/README.md)
- [net/transport](../libraries/net/transport/README.md)
- [net/tcp](../libraries/net/tcp/README.md)
- [net/stcp](../libraries/net/stcp/README.md)
- [net/yamux](../libraries/net/yamux/README.md)
- [net/quic](../libraries/net/quic/README.md)
- [multiformats](../libraries/multiformats/README.md)
- [net/p2p](../libraries/net/p2p/README.md)
- [plugins](../plugins/README.md)
- [tui](../libraries/tui/README.md)

## Engineering History

- [iterations](iterations) contains implementation decision history. Use it for
  context and rationale, not as the current API guide.
- [donors](donors) contains donor traceability: accepted and rejected patterns
  from upstream/reference projects.
- Historical migration notes live under [migration](migration) and are not part
  of the current public API guide.

If a document describes only one library's local API, it belongs in that
library's README. If it describes a design spanning multiple libraries, it
belongs under `docs/`.
