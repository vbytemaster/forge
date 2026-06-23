---
name: create-forge-plugin
description: Use when adding or refactoring an official Forge plugin package, public plugin modules, plugin API/config/lifecycle, package component, or plugin boundary review.
---

# Create Forge Plugin

Use this before creating or reshaping an official Forge plugin.

## Start

1. Read repo-root `AGENTS.md`.
2. Apply the repo namespace and target naming rules exactly.
3. Inspect existing `plugins/<family>/<name>` packages and follow current CMake/module patterns.
4. Keep Forge neutral: no downstream product vocabulary, storage policy, billing, S3, Storlane-specific semantics, or business workflow.

## Public Shape

- Put official plugins under `plugins/<family>/<name>`.
- Use leaf namespace owner shape:
  `namespace forge::plugins::<family>::<name> { class plugin; class api; ... }`.
- Keep grouping namespaces like `forge::plugins::p2p`, `forge::plugins::http` and
  `forge::plugins::crypto` empty.
- Families should mirror domain libraries when one exists. For crypto service
  plugins, use `forge::plugins::crypto::<role>` such as `signer` or `secrets`,
  not activity/provider names such as `signing::provider` or
  `secret::provider`.
- Public modules are slices, not umbrellas: `types`, `exceptions`, `api`, `plugin`.
- Public module names mirror the leaf namespace, for example
  `forge.plugins.crypto.signer.plugin` and `forge.plugins.http.server.api`.
- Plugin id and main API contract id use the same nested id, for example
  `forge.plugins.crypto.signer`.
- Extra plugin-owned API contracts use suffix ids, for example
  `forge.plugins.p2p.node.diagnostics_source`.
- Config sections use nested config paths without `forge.` prefix, for example
  `plugins.crypto.signer`, `plugins.crypto.secrets`, `plugins.http.server`.
- Do not create aggregate-only `.cppm` modules.
- Do not leave empty public include folders.

## API Boundary

- Plugin owns lifecycle, config, runtime resources, and startup/shutdown.
- API exposes typed local contracts only.
- API must not become a mini plugin: no `configure`, `startup`, `shutdown`, `request_stop`, raw route mutation, raw server mutation, diagnostics/status unless that is the plugin's explicit domain.
- Prefer `publish<Interface>()`, `remote<T>()`, `subscribe(...)`, `sign(...)` style capabilities over raw backend access.
- Do not expose `forge::http::router`, raw `forge::http::api::binding_plan` publication, `get/post/put/del` route APIs, or backend internals from infrastructure plugin APIs.
- Register API contracts with `FORGE_API(...)`; use `FORGE_API_METHOD_TYPED(...)` for overloaded methods.

## Config

- Define config DTOs in `types.cppm`.
- Use `BOOST_DESCRIBE_STRUCT` for every public config DTO.
- Define explicit `forge::schema::rules<T>` for names, defaults, ranges, secrets, object lists, and validators.
- Decode with `forge::config::decode<T>(...)`.
- Do not parse plugin config with `component_view::try_get`, `get_or`, manual `component_descriptor`, CLI parser types, YAML parser types, or environment access.
- Keep domain validation in the plugin only when it requires parsing endpoint/protocol/crypto/runtime semantics after schema decoding.

## Private Implementation

- Keep `plugin.cpp` as lifecycle glue.
- Split private code by domain, not by vague buckets.
- Good private file names: `plugin_impl`, concrete API facades such as `publisher_api` or `signer_api`, `config`, `protocol`, `descriptor_projection`, `message_projection`, `join_flow`.
- Avoid generic names: `state`, `api_facade`, `implementation`, `helpers`, `common`.
- Use `details/*.hxx` only for private declarations needed across plugin `.cpp` files; implementation stays in focused `.cpp` files at plugin root.
- Do not include another plugin's private `details` headers.

## CMake And Package

- Add one target following namespace mapping:
  `forge_plugins_<family>_<name>`.
- Add one package component:
  `plugins_<family>_<name>`.
- Link only required Forge targets; do not pull P2P/HTTP/crypto dependencies unless the plugin owns that boundary.
- Add the target to `forge_plugins` only after focused package tests exist.
- Do not add compatibility aliases for old flat plugin targets, components,
  module names, namespaces, plugin ids or API ids.

## Tests And Review Gates

- Add plugin tests for config descriptor/decode, lifecycle order, API behavior, late-use errors, duplicate contribution conflicts, and package component import.
- Add tests that descriptor id, main API contract id, dependency ids and config
  section use the nested naming scheme.
- Add package tests for `find_package(Forge COMPONENTS plugins_<family>_<name>)`
  and `Forge::forge_plugins_<family>_<name>`.
- Add static gates for old flat/plugin-provider names, raw route APIs, manual
  config parsing, empty include folders, generic details files, and product vocabulary.
- Run targeted plugin build/tests first, then broader app/API/package tests.

## Stop Conditions

- Stop and ask before adding product policy, storage semantics, billing, auth policy, diagnostics/status APIs outside the plugin's explicit responsibility, or a new raw backend escape hatch.
- Stop if a plugin API starts needing its own lifecycle; that behavior belongs in the plugin or a separate focused plugin.
