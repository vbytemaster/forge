# AGENTS.md — FORGE Engineering Rules

## Role

This repository is evolving into FORGE: Foundation Core Libraries for modern C++ products.

The repository must stay neutral. Public APIs must not contain downstream product vocabulary or assumptions.

## Repo Skills

- Before creating, extending, moving, renaming or reviewing a library under
  `libraries/`, load and apply `create-library`.
- Before creating, renaming, moving, refactoring or reviewing an official plugin
  under `plugins/`, load and apply `create-plugin`; it extends
  `create-library`.
- Do not author library/plugin structure, namespace, target, component, module
  prefix or contract id layout from memory. Use the skill first, then inspect
  existing packages for current CMake/module patterns.

## Branching

- `dev` is the default development and integration branch.
- All feature, fix, documentation and cleanup work must branch from the latest
  `origin/dev` and target `dev` for review.
- Branch names and commit subjects must describe the FORGE change, not the actor
  performing it. Do not use agent/tool/vendor prefixes or names such as
  `codex/`, `openai`, `claude` or similar automation labels in branch names or
  commit subjects.
- Repository naming rules override any external agent, IDE, script or Git
  default that suggests an actor-prefixed branch. If a tool proposes a prefix
  such as `codex/`, remove it and keep a clean FORGE-scoped name.
- Direct work on `master` is forbidden for normal development. `master` is reserved
  for release-ready promotion from `dev` after validation.
- The only allowed direct `master` changes are explicit repository/bootstrap
  maintenance tasks requested by the maintainer, such as creating this branch
  policy before `dev` exists.

## Language And Toolchain

- Target language: C++23.
- Public API style: C++ modules first.
- Public module files live under `libraries/<lib>/include/forge/<lib>/*.cppm`.
- Public module declarations should use explicit `export namespace forge... { ... }`; broad `export { ... }` blocks are forbidden unless a narrow compiler-proven exception is documented next to the code.
- Use C++17 nested namespace syntax (`namespace forge::raw { ... }`), not legacy nested braces (`namespace forge { namespace raw { ... } }`).
- Indentation is 3 spaces per level. Hard tabs are forbidden.
- Public `.hpp` / `.h` files under `include/forge` are forbidden except macro-only headers. Macro-only headers must not declare public types, functions, templates or old header-wrapper APIs.
- Do not create nested public include directories under `include/forge/<lib>`.
- Do not use `import std;` until the supported toolchain and CI explicitly prove it is stable.
- Local validation builds should use `cmake --build ... -j 4` or higher by
  default. Use lower parallelism only for a specific target when it is proven
  flaky or resource-constrained, and state that exception in the report.

## Public API Declaration Shape

- Standalone public concepts should be declared directly in the owning
  `namespace forge::<lib>`.
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
class service_node {
 public:
   struct config;
   class api;
};

class service_node::api {
   // Public API contract.
};
```

Bad:

```cpp
class service_node {
 public:
   class api {
      // Large public contract hidden inside the owner class body.
   };
};
```

## Library Shape

- Detailed library file placement, extension semantics, `details/`, `_impl`
  file naming and `.cpp` pairing are owned by `create-library`; do not duplicate
  or apply those rules from memory.
- Prefer many small targets over one monolithic target.
- Each major layer must become its own target, for example:
  - `forge_core`
  - `forge_reflect`
  - `forge_raw`
  - `forge_schema`
  - `forge_config`
  - `forge_yaml`
  - `forge_program_options`
  - `forge_json`
  - `forge_api`
  - `forge_crypto`
  - `forge_runtime`
  - `forge_log`
  - `forge_app`
  - `forge_http`
  - `forge_websocket`
  - `forge_quic`
  - `forge_p2p`
- `forge_plugins`
- `forge_tui`
- Heavy classes that own sockets, event loops, crypto contexts, terminal state, or other external resources should use pimpl.
- Value types, protocol records, and simple POD-like structs should not use pimpl.
- Split libraries when dependencies, ownership or public surfaces diverge.
  Do not hide reverse imports or target cycles behind umbrella targets.

## Namespace And Target Naming

FORGE uses deterministic namespace, target and module naming. Replace `::` with
`_` for targets and with `.` for module prefixes, and prefix targets with
`forge_` where appropriate.

Intermediate grouping namespaces are empty. Public types live only in leaf
namespaces. Namespace depth may vary when the depth is rule-driven; variable
depth is not itself a naming mismatch.

The master test is is-a: for candidate `A::B`, ask whether `B` is a kind,
representation or variant of `A`.

- If yes, `A` may be a family root and `B` may be its member.
- If no, and `B` exposes, adapts, depends on or uses `A`, do not name it as
  `A::B`; root it in the dependent medium or artifact category instead.

Group by what the artifact is, not by an implementation detail:

- Library/domain artifacts use top-level `forge::<domain>`, for example
  `forge::core`, `forge::raw`, `forge::http`, `forge::p2p`, `forge::api`,
  `forge::app`, `forge::tui` and `forge::crypto`.
- Plugin artifact family/role, namespace, target, component, module prefix and
  contract id mapping are owned by `create-plugin`; apply that skill instead of
  re-deriving plugin layout here.
- Application host variants use `forge::app::<archetype>` when the archetype is
  a kind of application, for example `forge::app::tui` or
  `forge::app::daemon`. Do not put application variants under implementation
  library namespaces such as `forge::tui::app`.
- Bindings and adapters that expose a core over a medium are rooted in the
  medium, not under the core, when the core fails the is-a test. Examples:
  `forge::http::api` and `forge::transport::api`.

Core or foundation namespaces may be family roots only when the is-a test passes.
`api` remains a leaf because there are no kinds of API; HTTP and transport expose
API over their channels, so use `forge::http::api` and `forge::transport::api`,
never `forge::api::http` or `forge::api::transport`. `app` may be a family root
because daemon, CLI and TUI are kinds of application hosts.

Apply this verification order before adding or renaming a public namespace:

1. Identify what the artifact is: library, plugin, application host or binding.
2. For plugins, apply `create-plugin` and do not duplicate plugin family rules
   here.
3. For non-plugin artifacts, apply the matching tree: `forge::<domain>`,
   `forge::app::<archetype>` or `forge::<medium>::<core>`.
4. If a core/foundation namespace is involved, run the is-a test to decide
   whether it can be a family root.
5. Check the `::` to `_` to `.` mapping and empty grouping namespaces.

Do not treat "foundation namespaces are never parents" as an absolute rule, do
not treat variable namespace depth as a problem by itself, do not put symbols
into grouping namespaces, and do not put application variants under
implementation-library namespaces. For plugin family/role decisions, follow
`create-plugin`.

## Reflection And Serialization

- New canonical reflection uses Boost.Describe directly.
- Do not add broad reflection macro aliases over Boost.Describe.
- Legacy reflection macro APIs are forbidden in FORGE code.
- Old reflect macro families must not return as public reflection APIs; use Boost.Describe directly.
- Binary serialization compatibility with the old FC raw byte layout is a hard gate.
- Any replacement for raw serialization must prove byte-for-byte compatibility with golden tests.
- Reflection field order must be explicit and stable.
- Macro-only serialization declaration helpers such as `FORGE_DECLARE_SERIALIZATION`
  may exist for explicit template instantiation, but they must not become a
  reflection system or define field order. Field order remains Boost.Describe.

## JSON, YAML, And Schema

- JSON and YAML APIs must use namespace-style typed codec functions over described types plus an FORGE-owned schema layer.
- Legacy JSON class APIs, parser facade APIs and parser classes are forbidden.
- Backend parser types must not leak through public module interfaces.
- Glaze is the JSON/YAML backend dependency. `glz::*` types and Glaze reflection metadata must not appear in public `.cppm` files.
- Validation belongs in schema rules, not in ad hoc parser code.
- Diagnostics must include clear paths, field names, and expected values.
- Secret-like fields must support redaction in configs, logs, diagnostics, and error context.
- `Boost.Program_options` is a backend dependency of `forge_program_options` only. App/plugin core must not expose `variables_map`, `options_description`, or other CLI parser types.
- Generic config merge order is schema defaults, config file,
  environment/custom adapters, then CLI. Foreground daemons use the stricter
  `run_daemon(...)` order: schema defaults, daemon defaults, YAML, `.env`,
  process env, daemon CLI, then app/plugin CLI.
- Environment and `.env` loading belongs to `forge_env`, not to `forge_app` or plugins.
- `forge_env` is a source adapter like `forge_program_options`: it maps process env
  and explicit `.env` files into `forge::config::document` using
  `component_registry`. It must not mutate global environment, implicitly search
  parent directories or expose downstream product variable names.
- Products decide source precedence explicitly before calling
  `application_shell::configure`.

## Errors And Logging

- FORGE exceptions are `std`-based and support typed categories through
  `forge::exceptions::coded_exception<Enum, Value>` plus structured redacted
  context through `forge::exceptions::context_error`.
- Public FORGE/app/network/API boundary failures should use typed exceptions
  under `forge::{lib}::exceptions::*`, without `_exception` suffix.
- `FORGE_THROW_EXCEPTION(ExceptionType, ...)` is the canonical typed throw macro.
- Use `FORGE_THROW_EXCEPTION(ExceptionType, ...)` when the concrete typed
  exception is known at the throw site.
- Use `FORGE_THROW_CODE(code_value, ...)` only when the exception code is computed
  at runtime, for example after mapping an engine status or retry result.
- `FORGE_DECLARE_EXCEPTION_CATEGORY` only declares `enum -> std::error_code`; it
  is not a throwing mechanism.
- New local `exceptions::raise(...)` helpers are forbidden. Add missing shared
  exception machinery to `forge.exceptions` instead of recreating switch-based
  throw helpers in each library.
- Use `FORGE_THROW`, `FORGE_ASSERT`, deadline checks and capture/log helpers with explicit `forge::exceptions::ctx(...)` or `forge::exceptions::secret(...)` fields.
- `FORGE_THROW(...)` is for generic context errors and internal legacy debt.
  Public library boundaries should prefer `FORGE_THROW_EXCEPTION` or
  `FORGE_THROW_CODE`.
- Legacy root error and singular exception namespace aliases are removed; use
  `forge::exceptions` directly.
- The old FC exception hierarchy, old declare/throw macros and variant-backed exception serialization are removed and must not reappear.
- Context capture must preserve source location and redact secret fields.
- Logging core should stay small: console/file/JSONL-style sinks and structured fields.
- External logging integrations must be optional adapters, not core dependencies.
- Do not log secrets, passphrases, private keys, token values, or raw key material.
- `forge_log` is synchronous in core. It must not import `forge_asio`, own a
  background queue or encode runtime policy; async logging belongs in a future
  adapter.
- Stack traces are diagnostic snapshots behind FORGE-owned public types. Prefer
  `std::stacktrace` when available, private Boost.Stacktrace fallback otherwise,
  and expose neither backend type from public modules.

## Crypto

- OpenSSL 3+ is the crypto backend baseline.
- There must be one OpenSSL implementation selected in the build graph.
- Do not add BoringSSL.
- Do not shell out to an external `openssl` binary for key or certificate generation.
- Specialized crypto libraries may remain optional targets when they have clear tests and isolated dependencies.
- K1 compatibility must not be replaced with generic OpenSSL ECDSA behavior.
- `forge.crypto.base58` must expose byte-friendly APIs using
  `std::span<const std::uint8_t>` and `std::vector<std::uint8_t>` for new
  multiformats/libp2p work. Existing `char` / `std::vector<char>` overloads may
  remain as compatibility wrappers, but new network code must not scatter
  casts between `char` and byte containers.
- `forge_crypto` stays synchronous and low-level. Do not import `forge_asio`,
  schedulers, threads or runtime policy into crypto primitives.
- WebAuthn parsing must stay private to `forge_crypto` and must not reintroduce a
  public or vendored JSON parser dependency.

## Runtime And Networking

- Async APIs for heavy operations should use `boost::asio::awaitable<T>`.
- Synchronous wrappers are allowed, but must not be the only API for heavy operations.
- Boost.Asio and Boost.Beast are valid dependencies for future runtime and network targets.
- Legacy networking code from the old codebase must not define the new network API.
- The network family is a set of independent root libraries: `forge_http`, `forge_websocket`, `forge_quic`, and `forge_p2p`.
- `forge_api` is the neutral typed contract layer used by app/network bindings; it
  must not import `forge_app`, `forge_http`, `forge_websocket`, `forge_quic` or
  `forge_p2p`.
- HTTP API bindings must preserve native HTTP route/path/status semantics; do
  not force all APIs into a frame-only `POST /rpc` model.
- WebSocket, QUIC and P2P API bindings use `forge::api::frame` and the shared
  `forge::api::error_payload`; protocol-specific duplicate error DTOs are
  forbidden.
- API binding builders must not expose decorative options. Every public option
  such as codec, frame size, max inflight, deadline, peer policy or middleware
  must affect runtime behavior and be covered by tests.
- HTTP-specific middleware belongs to `forge_http` router composition or the
  `forge::plugins::http::server` plugin facade.
  Protocol-neutral trace/authz/metrics/limits logic belongs to
  `forge::api::interceptor(...)`.
- Do not create `libraries/network`, legacy net-prefixed target, module, or namespace forms.
- Runtime workers must have explicit cancellation, bounded queues where needed, and deterministic shutdown.
- Do not introduce `std::async`, ad hoc polling loops, or unmanaged background threads as core runtime behavior.

## Plugins

Структура и нейминг плагинов заданы скиллом `create-plugin` (который расширяет
`create-library`). Перед созданием, переименованием ИЛИ правкой плагина — загрузи
и применяй `create-plugin`. Не авторь структуру/namespace/target плагина по памяти.
Правила здесь не дублируются.

## App And Plugins

- `forge_app` provides an opinionated `application_shell` for production
  programs. Prefer it for new services instead of copying runtime, scheduler,
  API registry, signals, events, diagnostics, plugin context and application runtime
  members into every product application.
- `application_shell` owns lifecycle order: collect app and plugin config,
  merge defaults with input document, configure app hook, configure plugins,
  provide app APIs, plugins provide APIs, initialize plugins, startup plugins, request stop and
  reverse shutdown.
- Plugins own behavior and lifecycle. APIs expose typed contracts; they
  must not become fake lifecycle modules.
- Ready-made infrastructure plugins may own transport/runtime lifecycle and
  publish narrow `forge_api` capabilities for product plugins, but they must not
  own product business logic.
- Plugin-specific exception families live with the owning plugin module, not in
  a shared catch-all plugin exceptions module. Concrete module/namespace layout
  is governed by `create-plugin`.
- `forge::plugins::p2p::node` is the production owner for a shared P2P node inside
  an application. It owns bootstrap, route/API contribution mounting, local
  endpoint reporting and typed remote API access; product plugins must not
  create parallel P2P nodes or call raw `p2p::node` path/relay primitives when
  the plugin owns the node.
- Production P2P network mechanics belong to `forge_p2p`, not to
  `forge::plugins::p2p::node`. FORGE's P2P direction is a clean C++23
  libp2p-compatible implementation: FORGE public APIs stay FORGE/Boost-style, but
  declared libp2p protocols must be wire-compatible with go-libp2p and
  rust-libp2p. Endpoint/address encoding, Peer ID, supported key families
  (Ed25519, Secp256k1, ECDSA and RSA), protocol negotiation, Identify, Ping,
  persistent peer/path store, AutoNAT/reachability, Circuit Relay/relay manager,
  AutoRelay, DCUtR/hole punching, DHT/rendezvous, pubsub/gossip, network limits,
  backpressure and network metrics must be added at the network layer. The
  plugin only maps config, owns app lifecycle, mounts route/API contributions
  and exposes safe local APIs.
- Public P2P APIs should use FORGE/Boost-style vocabulary such as `endpoint`,
  `resolver`, `listener`, `connector`, `session`, `stream` and `protocol_id`.
  libp2p terms such as `multiaddr` describe the compatibility wire/text format;
  they must not force C-style names into FORGE public APIs.
- libp2p compatibility must be evidence-based. For every libp2p protocol marked
  supported, tests must include spec-derived cases, donor-derived cases from
  go-libp2p/rust-libp2p and live interop coverage. A test that is merely
  "similar to libp2p" is not enough.
- Keep AutoNAT, AutoRelay, DHT, rendezvous, pubsub/gossip and relay discovery in
  `forge_p2p`. If a network-level behavior is missing, expose a typed
  unsupported/limited behavior or implement it in `forge_p2p`; do not hide it
  above the network layer.
- Durable P2P delivery in FORGE is pluggable, not storage-bound. If needed, it
  belongs to a focused future plugin or product service, not to the
  `forge::plugins::p2p::node` host facade.
- Plugin enable/disable is application-shell-owned config under
  `plugins.<family>.<name>.enabled`. Products must not manually distribute plugin
  selection from their own monolithic config object as the primary path.
- Foreground daemon lifecycle should use `forge.app.runner` before inventing
  local signal loops. Service-mode adapters such as systemd, launchd or Windows
  services are a separate future layer, not part of the app shell contract.
- Normal foreground daemons should use `forge.app.daemon::run_daemon(...)` for
  YAML, `.env`, process env, daemon CLI, app/plugin CLI, defaults, help,
  check-config, print-effective-config, generated config and signal policy.
  Product `main(...)` functions should be thin factories for an
  `application_shell`, not generic config frameworks.
- `run_application(...)` remains the lower-level lifecycle runner for tests,
  embedded hosts and custom shells that already own a merged
  `forge::config::document`.
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
- Avoid dependencies in low-level targets if they are only needed by future FORGE targets.
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
  `Boost.Describe -> forge::raw::pack -> hash/sign/encrypt` instead of ad-hoc
  strings, JSON text or manual field concatenation.
- Runtime-sensitive README files must call out technical risks and
  anti-patterns that lead to abort-driven error handling, detached background
  work, secret leaks, unbounded queues or unstable shutdown states.
- Cross-cutting architecture belongs under `docs/`, not inside a single library README.
- Markdown links must be relative to the current repository and must point to existing files.
- Absolute local machine paths are forbidden in docs, examples and generated report markdown.
- Downstream product vocabulary belongs only in donor/provenance notes when explicitly needed; reusable FORGE docs must stay product-neutral.

## Current Iteration Guardrails

- Keep the repository name unchanged until repository migration is planned.
- Boost.Describe migration must preserve the raw binary compatibility test baseline.
- Do not move neutral libraries from downstream projects until the FORGE target structure exists.
- Do not remove crypto primitives in the first pruning pass.
- `forge_core` is a low-level foundation only. It must not import `forge.crypto`, `forge.raw`, `forge.variant`, `forge.json` or `forge.log`.
- `forge_core` may own neutral diagnostic helpers such as type names, but must not own Boost.Describe member traversal or variant conversion.
- `forge_reflect` owns only Boost.Describe metadata helpers. It must not import or link `forge_variant`.
- `forge_variant` owns described-type conversion to and from `forge::variant`; described variant mapping must not live in `forge_reflect`.
- The umbrella `forge` target must not collect external dependencies "just in case"; put each dependency on the target that owns the include/link usage.
- Installed consumers should link leaf targets such as `Forge::forge_raw`,
  `Forge::forge_crypto` or `Forge::forge_app` when they need a small dependency
  footprint. `Forge::forge` is intentionally the whole feature set.
- Raw serialization belongs only to `libraries/raw`; do not define `namespace forge::raw` or raw overloads in `core`.
- Filesystem/config/path-layout helpers are not part of the FORGE core foundation. Use `std::filesystem` directly or keep app-specific helpers in consuming projects.
- Removed FC-like source APIs must not return as public FORGE APIs: `forge::array`, `forge::fwd`, `forge::safe`, `forge::filesystem`, flat/interprocess containers, mock time and compatibility mutexes.
- Public time values use `std::chrono`. FORGE core may provide chrono formatting and old FC wire helpers, but old FC-style time source APIs must not return.
- Deterministic tests must pass explicit chrono values instead of relying on a global mock clock.
- Empty/self-export module files and aggregate-only module files are forbidden.
  Convenience aggregation belongs to CMake targets and package components, not
  to public `.cppm` files that only `export import` other modules.
- Public APIs must live under `libraries/<lib>/include/forge/<lib>/*.cppm`; root include trees and old compatibility include roots are forbidden.
- Real module interfaces must contain their declarations directly; `.cppm` files that only include private/public headers are forbidden.
- Macro-only `.hpp` files under `include/forge` are allowed only for preprocessor macros, because C++ modules cannot export macros.
- `libraries/<lib>/private/forge` is forbidden.
- Public modules are owned by their domain targets. Do not introduce a global module bridge, shared BMI pool, umbrella module-owner target or dependency shortcut for module ordering.
- Implementation `.cpp` files live directly in `libraries/<lib>/`; nested `src/` directories are forbidden for FORGE-owned libraries.
- Third-party source and submodules live under root `vendor/`; `libraries/` contains only FORGE-owned code.
- Public module interfaces must not hide private implementation inside `.cppm`; private implementation belongs in module implementation units or private root-level helpers.
