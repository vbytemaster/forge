# FCL Docs Index

This index points at current FCL-owned documentation. FCL is a neutral C++23
infrastructure framework and constructor substrate for distributed services,
DePIN systems, blockchain/control-plane runtimes and plugin-based daemons. It is
not a product layer and does not own downstream storage, billing,
authorization, Spring or Storlane semantics.

Per-library `README.md` files are the quick start and API guide for one library.
Documents below explain cross-library architecture decisions.

## Main Documents

| Document | Purpose |
| --- | --- |
| [roadmap.md](roadmap.md) | Post-1.0 direction, architecture gates and framework boundaries. |
| [runtime/asio-app.md](runtime/asio-app.md) | Runtime ownership, bounded scheduler, plugin lifecycle and rollback. |
| [web/http-websocket.md](web/http-websocket.md) | HTTP/WebSocket layering, routing, upgrade, retry and backpressure rules. |
| [network/quic-p2p.md](network/quic-p2p.md) | QUIC transport, P2P peer identity, protocol streams and failure model. |
| [tui/notcurses-component-library.md](tui/notcurses-component-library.md) | TUI value models, deterministic rendering, navigation and Notcurses boundary. |
| [codecs/json-yaml-glaze.md](codecs/json-yaml-glaze.md) | JSON/YAML API shape, Glaze backend isolation and diagnostics. |
| [config/schema-config-program-options.md](config/schema-config-program-options.md) | Schema, config documents, env/CLI adapters, merge order and redaction. |
| [fcl_concept_ru.md](fcl_concept_ru.md) | Original Russian concept and long-form design motivation. |

## Blueprints

Blueprints are planning documents for possible future FCL directions. They are
not current public API commitments.

| Blueprint | Purpose |
| --- | --- |
| [blockchain-constructor](blueprints/blockchain-constructor/README.md) | Planning map for neutral FCL building blocks that can support blockchain and DePIN construction without turning FCL into a product runtime. |

## Library Guides

Each library guide must be useful without reading source first:

- [core](../libraries/core/README.md)
- [exception](../libraries/exceptions/README.md)
- [reflect](../libraries/reflect/README.md)
- [variant](../libraries/variant/README.md)
- [raw](../libraries/raw/README.md)
- [json](../libraries/json/README.md)
- [yaml](../libraries/yaml/README.md)
- [schema](../libraries/schema/README.md)
- [config](../libraries/config/README.md)
- [program_options](../libraries/program_options/README.md)
- [env](../libraries/env/README.md)
- [api](../libraries/api/README.md)
- [api_transport](../libraries/api_transport/README.md)
- [crypto](../libraries/crypto/README.md)
- [log](../libraries/log/README.md)
- [otlp](../libraries/otlp/README.md)
- [asio](../libraries/asio/README.md)
- [app](../libraries/app/README.md)
- [http](../libraries/http/README.md)
- [websocket](../libraries/websocket/README.md)
- [transport](../libraries/transport/README.md)
- [tcp](../libraries/tcp/README.md)
- [stcp](../libraries/stcp/README.md)
- [yamux](../libraries/yamux/README.md)
- [quic](../libraries/quic/README.md)
- [multiformats](../libraries/multiformats/README.md)
- [p2p](../libraries/p2p/README.md)
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
