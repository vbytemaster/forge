# FCL Roadmap

FCL развивается как набор нейтральных C++23 foundation-библиотек. Цель текущей
ветки — release candidate, который можно подключать в downstream products без
возврата старого FC source API and без переноса продуктовой семантики обратно в
FCL.

## Current Release Candidate Scope

- Module-first public API under `libraries/<lib>/include/fcl/<lib>/*.cppm`.
- Boost.Describe as canonical reflection metadata.
- `fcl_raw` byte compatibility for retained old FC wire layouts.
- Std-based exception context instead of old exception hierarchy.
- Std chrono instead of old FC time aliases.
- Glaze-backed JSON/YAML codec API.
- Async app/runtime stack over Boost.Asio.
- Separate HTTP, WebSocket, QUIC and P2P libraries.
- Optional Notcurses-backed TUI library.
- Production-grade library READMEs and cross-cutting docs.
- Synchronous logger v2 with structured records, sinks, redaction and private
  stacktrace backend.
- CMake install/export package with external consumer smoke.
- Buildable app/exception examples that downstream programs can copy as
  starting patterns.

## Library Families

- [Runtime + App](runtime/asio-app.md): runtime ownership, scheduler,
  backpressure and async plugin lifecycle.
- [HTTP + WebSocket](web/http-websocket.md): web/control-plane substrate,
  routing, middleware, upgrades and retry boundaries.
- [QUIC + P2P](network/quic-p2p.md): secure transport, peer identity, protocol
  streams, relay and path selection.
- [TUI](tui/notcurses-component-library.md): terminal value models, render
  helpers, navigation and backend isolation.
- [Codecs](codecs/json-yaml-glaze.md): JSON/YAML namespace APIs, Glaze backend
  boundary and diagnostics.
- [Config](config/schema-config-program-options.md): schema rules, neutral
  config documents, env/CLI adapters and redaction.
- [Migration Guide](migration/storlane-fc-to-fcl.md): target mapping, raw
  compatibility, Boost.Describe, chrono, exception and logger migration.

## Release Gates

Build/test gates:

```bash
cmake --build build/fcl-debug -j 1 \
  --target fcl test_fcl test_fcl_exception test_fcl_raw test_fcl_json test_fcl_crypto \
  test_fcl_asio test_fcl_app test_fcl_schema test_fcl_config test_fcl_yaml \
  test_fcl_program_options test_fcl_env test_fcl_http_websocket test_fcl_quic_p2p test_fcl_tui

ctest --test-dir build/fcl-debug --output-on-failure
git diff --check
```

Architecture gates:

- No public `<fc/...>` includes, `namespace fc`, `FC_REFLECT` or `FCL_REFLECT`.
- No backend parser/terminal/network types in public module interfaces.
- No umbrella target carrying external dependencies "just in case".
- No nested public include directories under `include/fcl/<lib>`.
- No absolute local machine paths in docs.
- Every library has a useful README with examples and ownership boundaries.

Security gates:

- Secret-bearing examples use explicit redaction.
- Crypto docs and code paths do not rely on shell-out generation.
- TLS/P2P verification failures are correctness failures.
- UI and HTTP helpers are not documented as authority/security boundaries.

## Remaining Before Downstream Migration

- Confirm full regression on the target toolchain.
- Re-run package install plus external `find_package(FCL CONFIG REQUIRED)`
  consumer smoke after review fixes.
- Run external review focused on documentation quality, architecture boundaries,
  dependency hygiene, security and production readiness.

## Out Of Scope For This Candidate

- Reintroducing source-level `fc::...` compatibility.
- A full schema migration framework.
- Browser UI or product admin flows.
- Product-specific protocol, storage, billing, authorization or deployment
  semantics.
