# Schema + Config + Program Options

This document explains the configuration stack across `fcl_schema`,
`fcl_config`, `fcl_yaml`, `fcl_json`, `fcl_program_options` and `fcl_app`.

Local guides:

- [schema](../../libraries/schema/README.md)
- [config](../../libraries/config/README.md)
- [program_options](../../libraries/program_options/README.md)
- [yaml](../../libraries/yaml/README.md)
- [json](../../libraries/json/README.md)
- [app](../../libraries/app/README.md)

## Задача

Libraries and plugins must describe configuration once, while programs choose
which input adapters are active. A plugin should not know whether `bind-port`
came from YAML, JSON, environment or `--http.bind-port=9090`.

## Ownership

- `fcl_schema` describes typed field rules and diagnostics.
- `fcl_config` stores neutral documents, merges layers, decodes types and
  redacts secrets.
- `fcl_yaml` and `fcl_json` are file/text codec adapters.
- `fcl_program_options` is the CLI adapter over Boost.Program_options.
- `fcl_app` consumes `component_view` and never sees parser backend types.

## End-To-End Flow

```text
Boost.Describe struct
  -> fcl_schema::rules<T>
  -> fcl_config::component_descriptor
  -> YAML/JSON/CLI adapters produce config::document
  -> merge(defaults, file, env/custom, cli)
  -> decode<T>(document, section)
  -> plugin.configure(component_view)
```

## Merge Order

The default order is:

1. schema defaults;
2. config file;
3. environment/custom adapters;
4. CLI.

Adapters do not hard-code precedence. Programs compose documents through
`fcl::config::merge`.

## Diagnostics

Diagnostics are stable machine-readable entries:

- `path` — `http.bind-port`;
- `code` — for example `schema.range`, `config.unknown`,
  `program_options.convert`;
- `level` — info/warning/error/critical;
- `message` — human-facing text.

This lets CLIs, TUI and HTTP admin surfaces render the same errors differently
without re-parsing exception text.

## Redaction

Secret fields are declared in schema. `fcl_config::redact(document, registry)`
applies that metadata before output. Redaction is not encryption and does not
replace vault/secret storage.

## Typical Integration Shape

```cpp
auto registry = runtime.describe_config();
auto yaml = fcl::yaml::load_document(config_path);
auto cli = fcl::program_options::parse(argc, argv, registry);
auto effective = fcl::config::merge({defaults, yaml.value, cli.document});
co_await runtime.configure(effective);
```

## Rejected Patterns

- Plugin-level `boost::program_options::variables_map`.
- Parser-specific config structs.
- Manual JSON/YAML builders for typed config.
- Logging raw config documents before redaction.

## Verification

- `test_fcl_schema`: rules/defaults/range/enum behavior.
- `test_fcl_config`: key paths, merge, decode, redaction and registry conflicts.
- `test_fcl_program_options`: dotted flags, booleans, repeated list values and
  parse diagnostics.
- `test_fcl_app`: config descriptor collection and configure-before-initialize.
