# fcl_config

`fcl_config` is the neutral configuration model between schema, YAML, JSON, CLI
and application plugins. It stores dotted-key documents, merges layers,
decodes typed objects and redacts secret fields using schema metadata.

## When To Use

- You need one config document shape independent of file format or CLI parser.
- You need merge precedence such as defaults `<` file `<` env/custom `<` CLI.
- A plugin/library wants to publish config descriptors without depending on
  Boost.Program_options or Glaze.

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
- `fcl.config` — aggregate import.

Target: `fcl_config`.

Dependencies: `fcl_schema`.

## Examples

### Build And Merge Documents

```cpp
import fcl.config;

auto defaults = fcl::config::document{};
defaults.set("http.bind-host", "127.0.0.1");
defaults.set("http.bind-port", 8080);

auto cli = fcl::config::document{};
cli.set("http.bind-port", 9090);

auto merged = fcl::config::merge({defaults, cli});
auto* port = merged.try_get("http.bind-port");
```

### Decode A Typed Section

```cpp
import fcl.config;

auto decoded = fcl::config::decode<http_config>(merged, "http");
if (!decoded.ok()) {
   for (const auto& entry : decoded.diagnostics.entries) {
      // entry.path, entry.code, entry.message
   }
}
```

### Redact Secrets Before Output

```cpp
import fcl.config;

auto registry = fcl::config::component_registry{};
registry.add(fcl::config::describe_component<http_config>("http"));

auto safe = fcl::config::redact(merged, registry);
```

### Configure A Component View

```cpp
import fcl.config;

auto view = fcl::config::component_view{merged, "http"};
auto host = view.get_or<std::string>("bind-host", "127.0.0.1");
auto port = view.get_or<std::uint16_t>("bind-port", 8080);
```

## Typical Mistakes

- Do not emit raw config documents to logs before redaction.
- Do not use ambiguous keys: `http.bind-port` and `http.port` may be aliases in
  schema but should not both be set by the same source unless the adapter has a
  deterministic conflict rule.
- Do not bypass `component_registry` for plugin config collection; duplicate
  fields/aliases must be detected before runtime startup.

## Tests

`test_fcl_config` covers dotted path handling, merge precedence, typed decode,
unknown/deprecated diagnostics, redaction and duplicate registry rejection.
