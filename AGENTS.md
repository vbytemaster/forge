# AGENTS.md — FCL Engineering Rules

## Role

This repository is evolving into FCL: Foundation Core Libraries for modern C++ products.

The repository must stay neutral. Public APIs must not contain downstream product vocabulary or assumptions.

## Branching

- `dev` is the default development and integration branch.
- All feature, fix, documentation and cleanup work must branch from the latest
  `origin/dev` and target `dev` for review.
- Direct work on `main` is forbidden for normal development. `main` is reserved
  for release-ready promotion from `dev` after validation.
- The only allowed direct `main` changes are explicit repository/bootstrap
  maintenance tasks requested by the maintainer, such as creating this branch
  policy before `dev` exists.

## Language And Toolchain

- Target language: C++23.
- Public API style: C++ modules first.
- Public module files live under `libraries/<lib>/include/fcl/<lib>/*.cppm`.
- Public module declarations should use explicit `export namespace fcl... { ... }`; broad `export { ... }` blocks are forbidden unless a narrow compiler-proven exception is documented next to the code.
- Use C++17 nested namespace syntax (`namespace fcl::raw { ... }`), not legacy nested braces (`namespace fcl { namespace raw { ... } }`).
- Indentation is 3 spaces per level. Hard tabs are forbidden.
- Public `.hpp` / `.h` files under `include/fcl` are forbidden except macro-only headers. Macro-only headers must not declare public types, functions, templates or old header-wrapper APIs.
- Do not create nested public include directories under `include/fcl/<lib>`.
- Do not use `import std;` until the supported toolchain and CI explicitly prove it is stable.

## Public API Declaration Shape

- Standalone public concepts should be declared directly in the owning
  `namespace fcl::<lib>`.
- Nested public types are allowed only when the type is truly owned by the
  enclosing class, such as short `config`, `options`, `result`, `token`,
  enum/value wrappers or other small companion records.
- If a nested type is a full public contract, interface or API, the owner class
  body should contain only a forward declaration such as `class api;`, and the
  contract body should be declared separately as `class owner::api { ... };`
  immediately below the owner.
- Do not write a nested type inline inside the owner class when it has virtual
  methods, many methods, its own pimpl, nested option/result families or is a
  consumer-facing interface in its own right.
- Implementation details must stay as private forward declarations such as
  `struct impl;` or `class api::impl;`, with definitions in the `.cpp`. Do not
  create top-level public `*_impl` vocabulary for implementation types.

Good:

```cpp
class p2p_node {
 public:
   struct config;
   class api;
};

class p2p_node::api {
   // Public API contract.
};
```

Bad:

```cpp
class p2p_node {
 public:
   class api {
      // Large public contract hidden inside the owner class body.
   };
};
```

## Library Shape

- Prefer many small targets over one monolithic target.
- Each major layer must become its own target, for example:
  - `fcl_core`
  - `fcl_reflect`
  - `fcl_raw`
  - `fcl_schema`
  - `fcl_config`
  - `fcl_yaml`
  - `fcl_program_options`
  - `fcl_json`
  - `fcl_api`
  - `fcl_crypto`
  - `fcl_runtime`
  - `fcl_log`
  - `fcl_app`
  - `fcl_http`
  - `fcl_websocket`
  - `fcl_quic`
  - `fcl_p2p`
- `fcl_plugins`
- `fcl_tui`
- Heavy classes that own sockets, event loops, crypto contexts, terminal state, or other external resources should use pimpl.
- Value types, protocol records, and simple POD-like structs should not use pimpl.
- `_impl` in a file name is allowed only for a large pimpl owner implementation,
  for example `node.cpp` for `node` and `node_impl.cpp` for `node::impl`.
- Small pimpl implementations should stay in the owner `.cpp`; do not create a
  separate `_impl.cpp` unless it materially improves readability.
- Ordinary private `struct`/`class` helpers should use a semantic or type-based
  file name such as `operation_deadline.cpp`, `relay_transport.cpp` or
  `node_state.cpp`, not `_impl`.

## Reflection And Serialization

- New canonical reflection uses Boost.Describe directly.
- Do not add broad reflection macro aliases over Boost.Describe.
- Legacy reflection macro APIs are forbidden in FCL code.
- Old reflect macro families must not return as public reflection APIs; use Boost.Describe directly.
- Binary serialization compatibility with the old FC raw byte layout is a hard gate.
- Any replacement for raw serialization must prove byte-for-byte compatibility with golden tests.
- Reflection field order must be explicit and stable.
- Macro-only serialization declaration helpers such as `FCL_DECLARE_SERIALIZATION`
  may exist for explicit template instantiation, but they must not become a
  reflection system or define field order. Field order remains Boost.Describe.

## JSON, YAML, And Schema

- JSON and YAML APIs must use namespace-style typed codec functions over described types plus an FCL-owned schema layer.
- Legacy JSON class APIs, parser facade APIs and parser classes are forbidden.
- Backend parser types must not leak through public module interfaces.
- Glaze is the JSON/YAML backend dependency. `glz::*` types and Glaze reflection metadata must not appear in public `.cppm` files.
- Validation belongs in schema rules, not in ad hoc parser code.
- Diagnostics must include clear paths, field names, and expected values.
- Secret-like fields must support redaction in configs, logs, diagnostics, and error context.
- `Boost.Program_options` is a backend dependency of `fcl_program_options` only. App/plugin core must not expose `variables_map`, `options_description`, or other CLI parser types.
- Generic config merge order is schema defaults, config file,
  environment/custom adapters, then CLI. Foreground daemons use the stricter
  `run_daemon(...)` order: schema defaults, daemon defaults, YAML, `.env`,
  process env, daemon CLI, then app/plugin CLI.
- Environment and `.env` loading belongs to `fcl_env`, not to `fcl_app` or plugins.
- `fcl_env` is a source adapter like `fcl_program_options`: it maps process env
  and explicit `.env` files into `fcl::config::document` using
  `component_registry`. It must not mutate global environment, implicitly search
  parent directories or expose downstream product variable names.
- Products decide source precedence explicitly before calling
  `application_shell::configure`.

## Errors And Logging

- FCL exceptions are `std`-based and support typed categories through
  `fcl::exceptions::coded_exception<Enum, Value>` plus structured redacted
  context through `fcl::exceptions::context_error`.
- Public FCL/app/network/API boundary failures should use typed exceptions
  under `fcl::{lib}::exceptions::*`, without `_exception` suffix.
- `FCL_THROW_EXCEPTION(ExceptionType, ...)` is the canonical typed throw macro.
- Use `FCL_THROW_EXCEPTION(ExceptionType, ...)` when the concrete typed
  exception is known at the throw site.
- Use `FCL_THROW_CODE(code_value, ...)` only when the exception code is computed
  at runtime, for example after mapping an engine status or retry result.
- `FCL_DECLARE_EXCEPTION_CATEGORY` only declares `enum -> std::error_code`; it
  is not a throwing mechanism.
- New local `exceptions::raise(...)` helpers are forbidden. Add missing shared
  exception machinery to `fcl.exceptions` instead of recreating switch-based
  throw helpers in each library.
- Use `FCL_THROW`, `FCL_ASSERT`, deadline checks and capture/log helpers with explicit `fcl::exceptions::ctx(...)` or `fcl::exceptions::secret(...)` fields.
- `FCL_THROW(...)` is for generic context errors and internal legacy debt.
  Public library boundaries should prefer `FCL_THROW_EXCEPTION` or
  `FCL_THROW_CODE`.
- Legacy root error and singular exception namespace aliases are removed; use
  `fcl::exceptions` directly.
- The old FC exception hierarchy, old declare/throw macros and variant-backed exception serialization are removed and must not reappear.
- Context capture must preserve source location and redact secret fields.
- Logging core should stay small: console/file/JSONL-style sinks and structured fields.
- External logging integrations must be optional adapters, not core dependencies.
- Do not log secrets, passphrases, private keys, token values, or raw key material.
- `fcl_log` is synchronous in core. It must not import `fcl_asio`, own a
  background queue or encode runtime policy; async logging belongs in a future
  adapter.
- Stack traces are diagnostic snapshots behind FCL-owned public types. Prefer
  `std::stacktrace` when available, private Boost.Stacktrace fallback otherwise,
  and expose neither backend type from public modules.

## Crypto

- OpenSSL 3+ is the crypto backend baseline.
- There must be one OpenSSL implementation selected in the build graph.
- Do not add BoringSSL.
- Do not shell out to an external `openssl` binary for key or certificate generation.
- Specialized crypto libraries may remain optional targets when they have clear tests and isolated dependencies.
- K1 compatibility must not be replaced with generic OpenSSL ECDSA behavior.
- `fcl.crypto.base58` must expose byte-friendly APIs using
  `std::span<const std::uint8_t>` and `std::vector<std::uint8_t>` for new
  multiformats/libp2p work. Existing `char` / `std::vector<char>` overloads may
  remain as compatibility wrappers, but new network code must not scatter
  casts between `char` and byte containers.
- `fcl_crypto` stays synchronous and low-level. Do not import `fcl_asio`,
  schedulers, threads or runtime policy into crypto primitives.
- WebAuthn parsing must stay private to `fcl_crypto` and must not reintroduce a
  public or vendored JSON parser dependency.

## Runtime And Networking

- Async APIs for heavy operations should use `boost::asio::awaitable<T>`.
- Synchronous wrappers are allowed, but must not be the only API for heavy operations.
- Boost.Asio and Boost.Beast are valid dependencies for future runtime and network targets.
- Legacy networking code from the old codebase must not define the new network API.
- The network family is a set of independent root libraries: `fcl_http`, `fcl_websocket`, `fcl_quic`, and `fcl_p2p`.
- `fcl_api` is the neutral typed contract layer used by app/network bindings; it
  must not import `fcl_app`, `fcl_http`, `fcl_websocket`, `fcl_quic` or
  `fcl_p2p`.
- HTTP API bindings must preserve native HTTP route/path/status semantics; do
  not force all APIs into a frame-only `POST /rpc` model.
- WebSocket, QUIC and P2P API bindings use `fcl::api::frame` and the shared
  `fcl::api::error_payload`; protocol-specific duplicate error DTOs are
  forbidden.
- API binding builders must not expose decorative options. Every public option
  such as codec, frame size, max inflight, deadline, peer policy or middleware
  must affect runtime behavior and be covered by tests.
- HTTP-specific middleware belongs to `fcl.http.api`/router composition.
  Protocol-neutral trace/authz/metrics/limits logic belongs to
  `fcl::api::interceptor(...)`.
- Do not create `libraries/network`, legacy net-prefixed target, module, or namespace forms.
- Runtime workers must have explicit cancellation, bounded queues where needed, and deterministic shutdown.
- Do not introduce `std::async`, ad hoc polling loops, or unmanaged background threads as core runtime behavior.

## App And Plugins

- `fcl_app` provides an opinionated `application_shell` for production
  programs. Prefer it for new services instead of copying runtime, scheduler,
  API registry, signals, events, diagnostics, plugin context and application runtime
  members into every product application.
- `application_shell` owns lifecycle order: collect app and plugin config,
  merge defaults with input document, configure app hook, configure plugins,
  provide app APIs, plugins provide APIs, initialize plugins, startup plugins, request stop and
  reverse shutdown.
- Plugins own behavior and lifecycle. APIs expose typed contracts; they
  must not become fake lifecycle modules.
- Ready-made infrastructure plugins live in `fcl_plugins`. They may own
  transport/runtime lifecycle and publish narrow `fcl_api` capabilities for
  product plugins, but they must not own product business logic.
- Plugin-specific exception families live with the owning plugin module, not in
  a shared catch-all plugin exceptions module. For example,
  `fcl::plugins::p2p_node::exceptions::*` belongs to `fcl.plugins.p2p_node`.
- `fcl::plugins::p2p_node` is the production owner for a shared P2P node inside
  an application. It owns bootstrap, route/API contribution mounting, local
  endpoint reporting and typed remote API access; product plugins must not
  create parallel P2P nodes or call raw `p2p::node` path/relay primitives when
  the plugin owns the node.
- Production P2P network mechanics belong to `fcl_p2p`, not to
  `fcl_plugins::p2p_node`. FCL's P2P direction is a clean C++23
  libp2p-compatible implementation: FCL public APIs stay FCL/Boost-style, but
  declared libp2p protocols must be wire-compatible with go-libp2p and
  rust-libp2p. Endpoint/address encoding, Peer ID, supported key families
  (Ed25519, Secp256k1, ECDSA and RSA), protocol negotiation, Identify, Ping,
  persistent peer/path store, AutoNAT/reachability, Circuit Relay/relay manager,
  AutoRelay, DCUtR/hole punching, DHT/rendezvous, pubsub/gossip, network limits,
  backpressure and network metrics must be added at the network layer. The
  plugin only maps config, owns app lifecycle, mounts route/API contributions
  and exposes safe local APIs.
- Public P2P APIs should use FCL/Boost-style vocabulary such as `endpoint`,
  `resolver`, `listener`, `connector`, `session`, `stream` and `protocol_id`.
  libp2p terms such as `multiaddr` describe the compatibility wire/text format;
  they must not force C-style names into FCL public APIs.
- libp2p compatibility must be evidence-based. For every libp2p protocol marked
  supported, tests must include spec-derived cases, donor-derived cases from
  go-libp2p/rust-libp2p and live interop coverage. A test that is merely
  "similar to libp2p" is not enough.
- Keep AutoNAT, AutoRelay, DHT, rendezvous, pubsub/gossip and relay discovery in
  `fcl_p2p`. If a network-level behavior is missing, expose a typed
  unsupported/limited behavior or implement it in `fcl_p2p`; do not hide it
  above the network layer.
- Durable P2P delivery in FCL is pluggable, not storage-bound. If needed, it
  belongs to a focused future plugin or product service, not to the
  `p2p_node` host facade.
- Plugin enable/disable is application-shell-owned config under
  `plugins.<plugin-id>.enabled`. Products must not manually distribute plugin
  selection from their own monolithic config object as the primary path.
- Foreground daemon lifecycle should use `fcl.app.runner` before inventing
  local signal loops. Service-mode adapters such as systemd, launchd or Windows
  services are a separate future layer, not part of the app shell contract.
- Normal foreground daemons should use `fcl.app.daemon::run_daemon(...)` for
  YAML, `.env`, process env, daemon CLI, app/plugin CLI, defaults, help,
  check-config, print-effective-config, generated config and signal policy.
  Product `main(...)` functions should be thin factories for an
  `application_shell`, not generic config frameworks.
- `run_application(...)` remains the lower-level lifecycle runner for tests,
  embedded hosts and custom shells that already own a merged
  `fcl::config::document`.
- Public lifecycle methods on `application_shell` are not extension points.
  Derived applications implement only hooks named without app tautology:
  `on_describe_config`, `on_configure`, `on_register_plugins`,
  `on_provide` and optionally `on_run_foreground`.
- Do not add hook names that repeat the application context or another
  parallel application lifecycle vocabulary. The context already identifies the
  code as application-level.
- `application_builder` is the preferred production composition path for normal
  product applications. It builds an `application_shell` and must not define a
  second lifecycle model, own independent lifecycle state or bypass shell
  config/plugin ordering.
- Do not add builder sugar such as separate `threads(...)` convenience methods
  without a concrete consumer win. Runtime options already carry worker count
  and thread name together.
- Use a derived `application_shell` only as an advanced escape hatch when the
  application has substantial state or lifecycle customization that callbacks
  cannot express cleanly.
- Plugin-owned descriptor factories live on the owner type as
  `plugin_type::descriptor()`. Avoid free `*_descriptor()` functions when the
  owner type already names the concept.
- APIs define typed contracts.
- Plugins orchestrate behavior.
- Adapters connect concrete backends and external systems.
- Events may support diagnostics, but must not become hidden business-flow coupling.
- Lifecycle order and reverse shutdown order must be testable.
- Plugins describe config through `describe_config()` and receive typed views through `configure(...)`; plugin core must not parse CLI argv or own YAML parser state.
- Plugin lifecycle methods that may touch resources use `boost::asio::awaitable<void>`: `configure`, `initialize`, `startup`, and `shutdown`. `request_stop()` remains synchronous and `noexcept`.

## TUI

- Terminal UI primitives must be neutral and reusable.
- Backend terminal library types must not leak from public APIs.
- Rendering and headless tests must enforce redaction.
- UI is not a security boundary.

## Dependencies

- Keep dependencies explicit and target-scoped.
- Optional features must be behind CMake options.
- Avoid dependencies in low-level targets if they are only needed by future FCL targets.
- Donor or reference code must not become a build dependency unless explicitly accepted as a dependency.
- Domain targets must form a real directed acyclic graph. A shared CMake BMI-producer target is forbidden; each domain target owns its public `.cppm` files through `FILE_SET CXX_MODULES`.
- If a domain needs reverse imports to build, split the domain instead of hiding the cycle behind an umbrella target.

## Tests

- Every compatibility layer needs golden tests.
- Required test groups include raw serialization, reflection, YAML/JSON/schema, crypto, runtime, network, app lifecycle, logging, and TUI.
- Failing compatibility tests are blockers.
- Do not delete crypto tests during non-crypto cleanup.
- Static checks should enforce that removed legacy zones do not reappear.

## Documentation

- Every `libraries/<lib>` directory must have a `README.md`.
- Library README files describe only the local library: problem, target, public modules, dependencies, examples, tests and boundaries.
- A library README must be useful to a new user without reading implementation files first: state when to use the library, when not to use it, include working module imports and API-shaped examples, name common mistakes, and call out security/redaction concerns when relevant.
- README examples must use real public module names and symbols from the current tree. Do not document future API as if it already exists.
- README examples must teach production patterns, not test shortcuts. Crypto
  examples that hash, sign or encrypt product DTOs should prefer the path
  `Boost.Describe -> fcl::raw::pack -> hash/sign/encrypt` instead of ad-hoc
  strings, JSON text or manual field concatenation.
- Runtime-sensitive README files must call out technical risks and
  anti-patterns that lead to abort-driven error handling, detached background
  work, secret leaks, unbounded queues or unstable shutdown states.
- Cross-cutting architecture belongs under `docs/`, not inside a single library README.
- Markdown links must be relative to the current repository and must point to existing files.
- Absolute local machine paths are forbidden in docs, examples and generated report markdown.
- Downstream product vocabulary belongs only in donor/provenance notes when explicitly needed; reusable FCL docs must stay product-neutral.

## Current Iteration Guardrails

- Keep the repository name unchanged until repository migration is planned.
- Boost.Describe migration must preserve the raw binary compatibility test baseline.
- Do not move neutral libraries from downstream projects until the FCL target structure exists.
- Do not remove crypto primitives in the first pruning pass.
- `fcl_core` is a low-level foundation only. It must not import `fcl.crypto`, `fcl.raw`, `fcl.variant`, `fcl.json` or `fcl.log`.
- `fcl_core` may own neutral diagnostic helpers such as type names, but must not own Boost.Describe member traversal or variant conversion.
- `fcl_reflect` owns only Boost.Describe metadata helpers. It must not import or link `fcl_variant`.
- `fcl_variant` owns described-type conversion to and from `fcl::variant`; described variant mapping must not live in `fcl_reflect`.
- The umbrella `fcl` target must not collect external dependencies "just in case"; put each dependency on the target that owns the include/link usage.
- Installed consumers should link leaf targets such as `FCL::fcl_raw`,
  `FCL::fcl_crypto` or `FCL::fcl_app` when they need a small dependency
  footprint. `FCL::fcl` is intentionally the whole feature set.
- Raw serialization belongs only to `libraries/raw`; do not define `namespace fcl::raw` or raw overloads in `core`.
- Filesystem/config/path-layout helpers are not part of the FCL core foundation. Use `std::filesystem` directly or keep app-specific helpers in consuming projects.
- Removed FC-like source APIs must not return as public FCL APIs: `fcl::array`, `fcl::fwd`, `fcl::safe`, `fcl::filesystem`, flat/interprocess containers, mock time and compatibility mutexes.
- Public time values use `std::chrono`. FCL core may provide chrono formatting and old FC wire helpers, but old FC-style time source APIs must not return.
- Deterministic tests must pass explicit chrono values instead of relying on a global mock clock.
- Empty/self-export module files are forbidden. Aggregate modules are allowed only as library-level `export import` lists.
- Public APIs must live under `libraries/<lib>/include/fcl/<lib>/*.cppm`; root include trees and old compatibility include roots are forbidden.
- Real module interfaces must contain their declarations directly; `.cppm` files that only include private/public headers are forbidden.
- Macro-only `.hpp` files under `include/fcl` are allowed only for preprocessor macros, because C++ modules cannot export macros.
- `libraries/<lib>/private/fcl` is forbidden.
- Public modules are owned by their domain targets. Do not introduce a global module bridge, shared BMI pool, umbrella module-owner target or dependency shortcut for module ordering.
- Implementation `.cpp` files live directly in `libraries/<lib>/`; nested `src/` directories are forbidden for FCL-owned libraries.
- Third-party source and submodules live under root `vendor/`; `libraries/` contains only FCL-owned code.
- Public module interfaces must not hide private implementation inside `.cppm`; private implementation belongs in module implementation units or private root-level helpers.
