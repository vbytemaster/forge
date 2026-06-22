# fcl_config

`fcl_config` is the neutral configuration model between schema, YAML, JSON,
environment, CLI and application plugins. It stores dotted-key documents, merges layers,
decodes typed objects and redacts secret fields using schema metadata.

## When To Use

- You need one config document shape independent of file format or CLI parser.
- You need merge precedence such as defaults `<` file `<` `.env` `<` process env `<` CLI.
- A plugin/library wants to publish config descriptors without depending on
  Boost.Program_options, Glaze or environment parsing.

## When Not To Use

- Do not use `config::key_path` for filesystem paths; use `std::filesystem`.
- Do not store runtime mutable state in `config::document`.
- Do not put business authorization checks here.

## Public Modules

- `fcl.config.value` — scalar/list/object value tree.
- `fcl.config.key_path` — dotted config key helper.
- `fcl.config.document` — `document`, `merge`, `effective_document`.
- `fcl.config.component` — component descriptors, registry, views, redaction.
- `fcl.config.decode` — `decode<T>`, `defaults_for<T>`, `describe_component<T>`.
- `fcl.config.migration` — document migrations before typed decode.

Target: `fcl_config`.

Dependencies: `fcl_schema`.

## Examples

### Build And Merge Documents

```cpp
import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;

auto defaults = fcl::config::document{};
defaults.set("http.bind-host", "127.0.0.1");
defaults.set("http.bind-port", 8080);

auto cli = fcl::config::document{};
cli.set("http.bind-port", 9090);

auto merged = fcl::config::merge({defaults, cli});
auto* port = merged.try_get("http.bind-port");
```

### Merge Independent Source Adapters

`fcl_config` does not parse YAML, JSON, `.env` or argv itself. Those libraries
return documents, and the caller decides precedence.

```cpp
import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;
import fcl.env;
import fcl.program_options;
import fcl.yaml;

auto file = fcl::yaml::load_document(workspace / "config.yml");
auto dotenv = fcl::env::load_document(workspace / ".env", registry, {.prefix = "FCL_APP"});
auto env = fcl::env::read_process_document(registry, {.prefix = "FCL_APP"});
auto cli = fcl::program_options::parse(argc, argv, registry);

auto input = fcl::config::merge({
   file.value,
   dotenv.value,
   env.value,
   cli.document,
});
```

### Decode A Typed Section

```cpp
import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;

auto decoded = fcl::config::decode<http_config>(merged, "http");
if (!decoded.ok()) {
   for (const auto& entry : decoded.diagnostics.entries) {
      std::cerr << entry.path << " [" << entry.code << "] "
                << entry.message << "\n";
   }
}
```

Nested object lists are decoded through `fcl_schema` rules too. Diagnostics use
indexed paths such as `plugins.signing.provider.keys[0].private-key`, while the config
registry still sees `plugins.signing.provider.keys` as one object-list field for
redaction and source-adapter policy.

### Redact Secrets Before Output

```cpp
import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;

auto registry = fcl::config::component_registry{};
registry.add(fcl::config::describe_component<http_config>("http"));

auto safe = fcl::config::redact(merged, registry);
```

### Compose Sources Before Application Startup

Use `fcl_config` as a glue layer between source adapters. Application code owns the
precedence order; plugins only publish descriptors and receive a component view.

```cpp
import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;
import fcl.program_options;
import fcl.yaml;

auto registry = application.describe_config();
auto yaml = fcl::yaml::load_document(config_path);
auto cli = fcl::program_options::parse(argc, argv, registry);

if (!yaml.ok()) {
   report_diagnostics(yaml.diagnostics);
}
if (!cli.ok()) {
   report_diagnostics(cli.diagnostics);
}

if (yaml.ok() && cli.ok()) {
   auto effective = fcl::config::merge({
      fcl::config::effective_document(registry),
      yaml.value,
      cli.document,
   });

   auto safe_for_logs = fcl::config::redact(effective, registry);
   application.configure(effective);
}
```

Never print `effective` before redaction. It may contain tokens, private paths
or other operator-provided secrets.

### Configure A Component View

Plugins should usually pass a component view into `fcl::config::decode<T>()`.
Use direct `component_view::try_get()` / `get_or()` only for rare adapter code,
not as a second parser beside schema rules.

```cpp
import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;

auto view = fcl::config::component_view{merged, "http"};
auto decoded = fcl::config::decode<http_config>(view.source(), view.section());
```

### Migrate Before Typed Decode

Migrations are document-level cleanup for old config files. They run before
`decode<T>()`; schema remains responsible for typed validation.

```cpp
import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;

auto plan = fcl::config::migration_plan{};
plan.step(0, 1, "rename http port", [](fcl::config::document& doc) {
   static_cast<void>(doc.rename("http.port", "http.bind-port"));
});
plan.step(1, 2, "add default host", [](fcl::config::document& doc) {
   if (!doc.try_get("http.bind-host")) {
      doc.set("http.bind-host", "127.0.0.1");
   }
});

auto migrated = fcl::config::migrate(std::move(document), plan);
if (!migrated.ok()) {
   // migrated.diagnostics explains missing steps, future versions or apply errors.
} else {
   auto decoded = fcl::config::decode<http_config>(migrated.value, "http");
   if (!decoded.ok()) {
      report_diagnostics(decoded.diagnostics.entries);
   }
}
```

## Risks And Anti-Patterns

- Do not use `config::document` as a second application config framework. Application
  config remains typed structs plus schema rules.
- Do not merge invalid layers and hope later code recovers. Source adapter and
  decode diagnostics must stop startup before side effects.
- Do not emit effective config without redaction. Documents can contain tokens,
  paths and operator-provided secrets.

## Typical Mistakes

- Do not emit raw config documents to logs before redaction.
- Do not use ambiguous keys: `http.bind-port` and `http.port` may be aliases in
  schema but should not both be set by the same source unless the adapter has a
  deterministic conflict rule.
- Do not bypass `component_registry` for plugin config collection; duplicate
  fields/aliases must be detected before runtime startup.
- Do not hand-parse official plugin config from `component_view` when the data
  can be described as typed schema fields. Add schema support first, then decode
  through `fcl::config::decode<T>()`.
- Do not make a second generic config document/parser layer in a consuming
  application. Use `fcl_yaml`, `fcl_json`, `fcl_env` or `fcl_program_options` as
  source adapters over this document model.
- Do not turn migrations into application validation. Keep them mechanical:
  rename, remove or add defaults, then let `fcl_schema` validate the typed
  config.
- Do not put application validation that requires I/O, credentials or live network
  checks into `fcl_config`. Decode config first, then run application validation in
  the owning program/plugin.
- Do not treat config merge as recovery from invalid config. Diagnostics must
  fail startup before plugins initialize.

## Tests

`test_fcl_config` covers dotted path handling, merge precedence, typed decode,
unknown/deprecated diagnostics, redaction, flat component sections, document
migrations and duplicate registry rejection.
