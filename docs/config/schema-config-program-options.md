# Schema + Config + Source Adapters

This document explains the configuration stack across `forge_schema`,
`forge_config`, `forge_yaml`, `forge_json`, `forge_env`, `forge_program_options` and
`forge_app`.

Local guides:

- [schema](../../libraries/schema/README.md)
- [config](../../libraries/config/README.md)
- [program_options](../../libraries/program_options/README.md)
- [env](../../libraries/env/README.md)
- [yaml](../../libraries/yaml/README.md)
- [json](../../libraries/json/README.md)
- [app](../../libraries/app/README.md)

## Задача

Libraries and plugins must describe configuration once, while programs choose
which input adapters are active. A plugin should not know whether `bind-port`
came from YAML, JSON, environment or `--http.bind-port=9090`.

## Ownership

- `forge_schema` describes typed field rules and diagnostics.
- `forge_config` stores neutral documents, merges layers, decodes types and
  redacts secrets.
- `forge_yaml` and `forge_json` are file/text codec adapters.
- `forge_env` is the process environment and explicit `.env` adapter.
- `forge_program_options` is the CLI adapter over Boost.Program_options.
- `forge_app` consumes `component_view` and never sees parser backend types.

## End-To-End Flow

```text
Boost.Describe struct
  -> forge_schema::rules<T>
  -> forge_config::component_descriptor
  -> YAML/JSON/env/CLI adapters produce config::document
  -> merge(defaults, file, dotenv, process_env, cli)
  -> decode<T>(document, section)
  -> plugin.configure(component_view)
```

## Merge Order

The default order is:

1. schema defaults;
2. config file;
3. `.env`;
4. process environment/custom adapters;
5. CLI.

Adapters do not hard-code precedence. Programs compose documents through
`forge::config::merge`.

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

Secret fields are declared in schema. `forge_config::redact(document, registry)`
applies that metadata before output. Redaction is not encryption and does not
replace vault/secret storage.

## Typical Integration Shape

```cpp
auto registry = application.describe_config();
auto yaml = forge::yaml::load_document(config_path);
auto dotenv = forge::env::load_document(workspace / ".env", registry, {.prefix = "APP"});
auto env = forge::env::read_process_document(registry, {.prefix = "APP"});
auto cli = forge::program_options::parse(argc, argv, registry);
if (!yaml.ok()) {
   report_diagnostics(yaml.diagnostics);
}
if (!dotenv.ok()) {
   report_diagnostics(dotenv.diagnostics);
}
if (!env.ok()) {
   report_diagnostics(env.diagnostics);
}
if (!cli.ok()) {
   report_diagnostics(cli.diagnostics);
}

if (yaml.ok() && dotenv.ok() && env.ok() && cli.ok()) {
   auto effective = forge::config::merge({
      defaults,
      yaml.value,
      dotenv.value,
      env.value,
      cli.document,
   });
   application.configure(effective);
}
```

## Rejected Patterns

- Plugin-level backend CLI parser maps.
- Plugin-level `std::getenv()` or implicit `.env` discovery.
- Parser-specific config structs.
- Manual JSON/YAML builders for typed config.
- Logging raw config documents before redaction.

## Verification

- `test_forge_schema`: rules/defaults/range/enum behavior.
- `test_forge_config`: key paths, merge, decode, redaction and registry conflicts.
- `test_forge_program_options`: dotted flags, booleans, repeated list values and
  parse diagnostics.
- `test_forge_env`: dotenv grammar, env name mapping, aliases, conversions,
  unknowns and secret-safe examples.
- `test_forge_app`: config descriptor collection and configure-before-initialize.
