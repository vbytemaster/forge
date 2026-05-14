# FCL Config/Schema/App v1 Donor Traceability

## Summary

This pass introduces `fcl_schema`, `fcl_config`, `fcl_yaml`, `fcl_program_options`, and removes `Boost.Program_options` from `fcl_app` core. Parser backends remain backend-only dependencies; plugins describe configuration and receive typed config views before async lifecycle startup.

## Donor Matrix

| Donor file | Accepted pattern | Rejected pattern | FCL target | Test/proof |
| --- | --- | --- | --- | --- |
| `../bitstore-core/libraries/app/include/bitstore/app/plugin.hpp` | Plugins declare options before initialize/startup and have explicit lifecycle phases. | Exposing backend CLI parser result maps or option descriptors through plugin core. | `fcl_app::plugin::describe_config()`, `configure(...)`, async `initialize/startup/shutdown`. | `test_fcl_app` config ordering and async rollback tests. |
| `../bitstore-core/libraries/app/config_util.cpp` | Centralized config/default/override composition. | Product-specific CLI/config glue in app core. | `fcl_config::merge`, `component_registry`, `component_view`. | `test_fcl_config` merge precedence and decode tests. |
| `../donors/kubo/config/config.go` | Typed config structs and repo/config separation. | Raw map as public runtime configuration model. | `fcl_config::document` as neutral source model plus typed `decode<T>`. | `test_fcl_config`, `test_fcl_yaml`. |
| `../donors/kubo/core/commands/config.go` | Operator-visible config read/write commands over typed config. | Backend parser objects as public API. | `fcl_yaml` load/save returns `config::document`; no backend parser types in public API. | `test_fcl_yaml`; static grep for public backend leakage. |
| `../donors/containerd/cmd/containerd/server/config/config.go` | Plugin-scoped config sections, disabled/required plugin thinking, unknown/deprecated diagnostics. | TOML-specific API or containerd plugin model as FCL API. | `component_descriptor`, `component_registry`, deprecated/unknown diagnostics. | `test_fcl_config` unknown/deprecated/duplicate tests. |
| `../donors/syncthing/lib/config/config.go` | Defaults, validation, migration discipline, operator-visible config errors. | XML-specific config model and hidden parser errors. | `schema::rules<T>`, diagnostics with path/code/severity/message. | `test_fcl_schema`, `test_fcl_config`. |
| `../donors/syncthing/lib/config/wrapper.go` | Wrapper boundary around config state and redaction-friendly access. | Mutable global config maps. | `config::document`, `component_view`, `redact`. | `test_fcl_config` redaction test. |
| Glaze | Modern JSON/YAML parser/emitter backend only. | Leaking backend parser namespaces or reflection metadata into FCL public API. | `fcl_json`, `fcl_yaml`. | `test_fcl_json`, `test_fcl_yaml`; static grep. |
| `Boost.Program_options` | CLI argv parser backend only. | App/plugin core depending on Boost CLI parser types. | `fcl_program_options`. | `test_fcl_program_options`; static grep for app boundary. |

## Notes

- `Boost.Describe` is metadata, not validation. Validation rules live in `fcl_schema`.
- Merge order is fixed as schema defaults, config file, environment/custom adapters, then CLI.
- `fcl_app` depends on `fcl_config`, not `fcl_yaml` or `fcl_program_options`.
- C++ module enum reflection has a visibility caveat: enum descriptions should be declared in the owning module before generic enum conversion is instantiated. This pass keeps schema enum helpers and documents the rule; broader enum UX can be polished with the Boost.Describe/schema cleanup pass.
