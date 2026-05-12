# FCL Docs Index

This index points at current FCL-owned documentation. Per-library `README.md`
files are the quick start and API guide for one library. Documents below explain
cross-library architecture decisions.

## Main Documents

| Document | Purpose |
| --- | --- |
| [roadmap.md](roadmap.md) | Current release candidate scope, gates and migration boundary. |
| [runtime/asio-app.md](runtime/asio-app.md) | Runtime ownership, bounded scheduler, plugin lifecycle and rollback. |
| [web/http-websocket.md](web/http-websocket.md) | HTTP/WebSocket layering, routing, upgrade, retry and backpressure rules. |
| [network/quic-p2p.md](network/quic-p2p.md) | QUIC transport, P2P peer identity, protocol streams and failure model. |
| [tui/notcurses-component-library.md](tui/notcurses-component-library.md) | TUI value models, deterministic rendering, navigation and Notcurses boundary. |
| [codecs/json-yaml-glaze.md](codecs/json-yaml-glaze.md) | JSON/YAML API shape, Glaze backend isolation and diagnostics. |
| [config/schema-config-program-options.md](config/schema-config-program-options.md) | Schema, config documents, CLI adapter, merge order and redaction. |
| [fcl_concept_ru.md](fcl_concept_ru.md) | Original Russian concept and long-form design motivation. |

## Library Guides

Each library guide must be useful without reading source first:

- [core](../libraries/core/README.md)
- [exception](../libraries/exception/README.md)
- [reflect](../libraries/reflect/README.md)
- [variant](../libraries/variant/README.md)
- [raw](../libraries/raw/README.md)
- [json](../libraries/json/README.md)
- [yaml](../libraries/yaml/README.md)
- [schema](../libraries/schema/README.md)
- [config](../libraries/config/README.md)
- [program_options](../libraries/program_options/README.md)
- [crypto](../libraries/crypto/README.md)
- [log](../libraries/log/README.md)
- [asio](../libraries/asio/README.md)
- [app](../libraries/app/README.md)
- [http](../libraries/http/README.md)
- [websocket](../libraries/websocket/README.md)
- [quic](../libraries/quic/README.md)
- [p2p](../libraries/p2p/README.md)
- [tui](../libraries/tui/README.md)

## Engineering History

- [iterations](iterations) contains implementation decision history. Use it for
  context and rationale, not as the current API guide.
- [donors](donors) contains donor traceability: accepted and rejected patterns
  from upstream/reference projects.

If a document describes only one library's local API, it belongs in that
library's README. If it describes a design spanning multiple libraries, it
belongs under `docs/`.
