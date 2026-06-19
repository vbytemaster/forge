# FCL Roadmap

FCL развивается как нейтральный C++23 infrastructure framework и constructor
substrate. Он нужен для сборки распределённых сервисов, DePIN-сетей,
blockchain/control-plane runtimes, P2P сетей, plugin-based daemons and
transport/API layers без переноса продуктовой семантики в FCL.

После `v1.0.0` основной ориентир — не “дописать старый FC-слой”, а удерживать
чистые reusable boundaries: runtime, config, API contracts, transports, P2P,
plugins, telemetry and compatibility layers должны быть пригодны для серьёзных
потребителей, но не становиться Storlane/Spring/storage/billing runtime.

## Current Framework Surface

- Module-first public API under `libraries/<lib>/include/fcl/<lib>/*.cppm`.
- Boost.Describe as canonical reflection metadata.
- `fcl_raw` byte compatibility for retained old FC wire layouts.
- Typed `fcl_exceptions` categories plus redacted context instead of old
  exception hierarchy.
- Neutral `fcl_api` contracts for in-process plugin APIs and transport API
  bindings.
- `fcl_plugins` aggregate plus focused `fcl_plugin_*` targets for shared
  lifecycle-owned components such as P2P nodes, with narrow local APIs for
  route/binding contributions.
- Std chrono instead of old FC time aliases.
- Glaze-backed JSON/YAML codec API.
- Async app/runtime stack over Boost.Asio.
- Separate HTTP, WebSocket, QUIC and P2P libraries.
- Optional Notcurses-backed TUI library.
- Production-grade library READMEs and cross-cutting docs.
- Synchronous logger v2 with structured records, sinks, redaction and private
  stacktrace backend.
- CMake install/export package with external consumer smoke.
- API-over-transport, P2P plugin facade, resolver, diagnostics and PubSub
  plugin flows for typed service composition.
- OTLP log export and crash-spool resend as opt-in observability adapters.

## Library Families

- [Runtime + App](runtime/asio-app.md): runtime ownership, scheduler,
  backpressure and async plugin lifecycle.
- [API Contracts](../libraries/api/README.md): typed handles, descriptor
  macros, local/remote surfaces, API frames and shared error payloads.
- [HTTP + WebSocket](web/http-websocket.md): web/control-plane substrate,
  routing, middleware, upgrades and retry boundaries.
- [QUIC + P2P](network/quic-p2p.md): secure transport, peer identity, protocol
  streams, relay and path selection.
- [Transport substrate](../libraries/transport/README.md): reusable
  stream/session concepts, chunks, frame helpers and muxer substrate.
- [Infrastructure plugins](../plugins/README.md): lifecycle-owned P2P
  node, API resolver, diagnostics and PubSub facade.
- [Telemetry](../libraries/otlp/README.md): opt-in OTLP logs and crash evidence
  export.
- [TUI](tui/notcurses-component-library.md): terminal value models, render
  helpers, navigation and backend isolation.
- [Codecs](codecs/json-yaml-glaze.md): JSON/YAML namespace APIs, Glaze backend
  boundary and diagnostics.
- [Config](config/schema-config-program-options.md): schema rules, neutral
  config documents, env/CLI adapters and redaction.
- [Secret Provider](iterations/fcl-secret-provider-v1.md): planned neutral
  infrastructure plugin for local secret material, redacted source loading and
  purpose-gated AES-GCM/HKDF operations.
- Historical migration notes: target mapping, raw compatibility,
  Boost.Describe, chrono, exception and logger migration live under
  `docs/migration` and are not current API commitments.

## Release Gates

Build/test gates:

```bash
cmake --build build/fcl-debug -j 1 \
  --target fcl test_fcl test_fcl_exceptions test_fcl_raw test_fcl_json test_fcl_crypto \
  test_fcl_multiformats test_fcl_asio test_fcl_transport test_fcl_tcp test_fcl_stcp \
  test_fcl_yamux test_fcl_quic test_fcl_app test_fcl_schema test_fcl_config \
  test_fcl_yaml test_fcl_program_options test_fcl_env test_fcl_api \
  test_fcl_api_transport test_fcl_http_websocket test_fcl_quic_p2p \
  test_fcl_plugins test_fcl_otlp test_fcl_tui

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

## Ongoing Readiness Work

- Keep library READMEs aligned with public modules and actual targets.
- Keep donor traceability updated when compatibility behavior changes.
- Re-run package install plus external `find_package(FCL CONFIG REQUIRED)`
  consumer smoke before releases.
- Keep review focused on architecture boundaries, dependency hygiene, security
  and production readiness.

## Out Of Scope For FCL Core

- Reintroducing source-level `fc::...` compatibility.
- A full schema migration framework.
- Browser UI or product admin flows.
- Product-specific protocol, storage, billing, authorization or deployment
  semantics.
- Presenting blueprints as shipped public API.
