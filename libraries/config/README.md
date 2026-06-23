# forge_config

`forge_config` is the neutral configuration model between schema, YAML, JSON,
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

- `forge.config.value` — scalar/list/object value tree.
- `forge.config.key_path` — dotted config key helper.
- `forge.config.document` — `document`, `merge`, `effective_document`.
- `forge.config.component` — component descriptors, registry, views, redaction.
- `forge.config.decode` — `decode<T>`, `defaults_for<T>`, `describe_component<T>`.
- `forge.config.migration` — document migrations before typed decode.

Target: `forge_config`.

Dependencies: `forge_schema`.

## Examples

### Build And Merge Documents

```cpp
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;

auto defaults = forge::config::document{};
defaults.set("http.bind-host", "127.0.0.1");
defaults.set("http.bind-port", 8080);

auto cli = forge::config::document{};
cli.set("http.bind-port", 9090);

auto merged = forge::config::merge({defaults, cli});
auto* port = merged.try_get("http.bind-port");
```

### Merge Independent Source Adapters

`forge_config` does not parse YAML, JSON, `.env` or argv itself. Those libraries
return documents, and the caller decides precedence.

```cpp
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.env;
import forge.program_options;
import forge.yaml;

auto file = forge::yaml::load_document(workspace / "config.yml");
auto dotenv = forge::env::load_document(workspace / ".env", registry, {.prefix = "FORGE_APP"});
auto env = forge::env::read_process_document(registry, {.prefix = "FORGE_APP"});
auto cli = forge::program_options::parse(argc, argv, registry);

auto input = forge::config::merge({
   file.value,
   dotenv.value,
   env.value,
   cli.document,
});
```

### Decode A Typed Section

```cpp
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;

auto decoded = forge::config::decode<http_config>(merged, "http");
if (!decoded.ok()) {
   for (const auto& entry : decoded.diagnostics.entries) {
      std::cerr << entry.path << " [" << entry.code << "] "
                << entry.message << "\n";
   }
}
```

Nested object lists are decoded through `forge_schema` rules too. Diagnostics use
indexed paths such as `plugins.crypto.signer.keys[0].private-key`, while the config
registry still sees `plugins.crypto.signer.keys` as one object-list field for
redaction and source-adapter policy.

### Redact Secrets Before Output

```cpp
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;

auto registry = forge::config::component_registry{};
registry.add(forge::config::describe_component<http_config>("http"));

auto safe = forge::config::redact(merged, registry);
```

### Compose Sources Before Application Startup

Use `forge_config` as a glue layer between source adapters. Application code owns the
precedence order; plugins only publish descriptors and receive a component view.

```cpp
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.program_options;
import forge.yaml;

auto registry = application.describe_config();
auto yaml = forge::yaml::load_document(config_path);
auto cli = forge::program_options::parse(argc, argv, registry);

if (!yaml.ok()) {
   report_diagnostics(yaml.diagnostics);
}
if (!cli.ok()) {
   report_diagnostics(cli.diagnostics);
}

if (yaml.ok() && cli.ok()) {
   auto effective = forge::config::merge({
      forge::config::effective_document(registry),
      yaml.value,
      cli.document,
   });

   auto safe_for_logs = forge::config::redact(effective, registry);
   application.configure(effective);
}
```

Never print `effective` before redaction. It may contain tokens, private paths
or other operator-provided secrets.

### Configure A Component View

Plugins should usually pass a component view into `forge::config::decode<T>()`.
Use direct `component_view::try_get()` / `get_or()` only for rare adapter code,
not as a second parser beside schema rules.

```cpp
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;

auto view = forge::config::component_view{merged, "http"};
auto decoded = forge::config::decode<http_config>(view.source(), view.section());
```

### Migrate Before Typed Decode

Migrations are document-level cleanup for old config files. They run before
`decode<T>()`; schema remains responsible for typed validation.

```cpp
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;

auto plan = forge::config::migration_plan{};
plan.step(0, 1, "rename http port", [](forge::config::document& doc) {
   static_cast<void>(doc.rename("http.port", "http.bind-port"));
});
plan.step(1, 2, "add default host", [](forge::config::document& doc) {
   if (!doc.try_get("http.bind-host")) {
      doc.set("http.bind-host", "127.0.0.1");
   }
});

auto migrated = forge::config::migrate(std::move(document), plan);
if (!migrated.ok()) {
   // migrated.diagnostics explains missing steps, future versions or apply errors.
} else {
   auto decoded = forge::config::decode<http_config>(migrated.value, "http");
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
  through `forge::config::decode<T>()`.
- Do not make a second generic config document/parser layer in a consuming
  application. Use `forge_yaml`, `forge_json`, `forge_env` or `forge_program_options` as
  source adapters over this document model.
- Do not turn migrations into application validation. Keep them mechanical:
  rename, remove or add defaults, then let `forge_schema` validate the typed
  config.
- Do not put application validation that requires I/O, credentials or live network
  checks into `forge_config`. Decode config first, then run application validation in
  the owning program/plugin.
- Do not treat config merge as recovery from invalid config. Diagnostics must
  fail startup before plugins initialize.

## Tests

`test_forge_config` covers dotted path handling, merge precedence, typed decode,
unknown/deprecated diagnostics, redaction, flat component sections, document
migrations and duplicate registry rejection.
