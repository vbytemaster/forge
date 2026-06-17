---
name: create-fcl-plugin
description: Create or refactor official FCL plugins under root plugins/. Use when adding a plugin package, plugin public modules, plugin API/config/lifecycle, or reviewing plugin boundaries. Enforces namespace-owner layout, schema-driven config, typed FCL API contracts, focused private implementation files, and no mini-plugin APIs.
---

# Create FCL Plugin

Use this before creating or reshaping an official FCL plugin.

## Start

1. Read repo-root `AGENTS.md`.
2. Inspect existing `plugins/<name>` packages and follow current CMake/module patterns.
3. Keep FCL neutral: no downstream product vocabulary, storage policy, billing, S3, Storlane-specific semantics, or business workflow.

## Public Shape

- Put official plugins under `plugins/<name>`.
- Use namespace owner shape: `namespace fcl::plugins::<name> { class plugin; class api; ... }`.
- Public modules are slices, not umbrellas: `types`, `exceptions`, `api`, `plugin`.
- Keep plugin id, config section, API id, and module name explicit and non-tautological.
- Do not create aggregate-only `.cppm` modules.
- Do not leave empty public include folders.

## API Boundary

- Plugin owns lifecycle, config, runtime resources, and startup/shutdown.
- API exposes typed local contracts only.
- API must not become a mini plugin: no `configure`, `startup`, `shutdown`, `request_stop`, raw route mutation, raw server mutation, diagnostics/status unless that is the plugin's explicit domain.
- Prefer `publish<Interface>()`, `remote<T>()`, `subscribe(...)`, `sign(...)` style capabilities over raw backend access.
- Do not expose `fcl::http::router`, raw `fcl::http::api_binding` publication, `get/post/put/del` route APIs, or backend internals from infrastructure plugin APIs.
- Register API contracts with `FCL_API(...)`; use `FCL_API_METHOD_TYPED(...)` for overloaded methods.

## Config

- Define config DTOs in `types.cppm`.
- Use `BOOST_DESCRIBE_STRUCT` for every public config DTO.
- Define explicit `fcl::schema::rules<T>` for names, defaults, ranges, secrets, object lists, and validators.
- Decode with `fcl::config::decode<T>(...)`.
- Do not parse plugin config with `component_view::try_get`, `get_or`, manual `component_descriptor`, CLI parser types, YAML parser types, or environment access.
- Keep domain validation in the plugin only when it requires parsing endpoint/protocol/crypto/runtime semantics after schema decoding.

## Private Implementation

- Keep `plugin.cpp` as lifecycle glue.
- Split private code by domain, not by vague buckets.
- Good private file names: `plugin_impl`, concrete API facades such as `publisher_api` or `signing_api`, `config`, `protocol`, `descriptor_projection`, `message_projection`, `join_flow`.
- Avoid generic names: `state`, `api_facade`, `implementation`, `helpers`, `common`.
- Use `details/*.hxx` only for private declarations needed across plugin `.cpp` files; implementation stays in focused `.cpp` files at plugin root.
- Do not include another plugin's private `details` headers.

## CMake And Package

- Add one target: `fcl_plugin_<name>`.
- Add package component: `plugin_<name>`.
- Link only required FCL targets; do not pull P2P/HTTP/crypto dependencies unless the plugin owns that boundary.
- Add the target to `fcl_plugins` only after focused package tests exist.

## Tests And Review Gates

- Add plugin tests for config descriptor/decode, lifecycle order, API behavior, late-use errors, duplicate contribution conflicts, and package component import.
- Add static gates for removed surfaces, for example raw route APIs, manual config parsing, empty include folders, generic details files, and product vocabulary.
- Run targeted plugin build/tests first, then broader app/API/package tests.

## Stop Conditions

- Stop and ask before adding product policy, storage semantics, billing, auth policy, diagnostics/status APIs outside the plugin's explicit responsibility, or a new raw backend escape hatch.
- Stop if a plugin API starts needing its own lifecycle; that behavior belongs in the plugin or a separate focused plugin.
