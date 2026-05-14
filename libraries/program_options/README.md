# fcl_program_options

`fcl_program_options` is the CLI adapter for FCL config. It uses
Boost.Program_options internally, but returns `fcl::config::document` and
`fcl::schema::diagnostic` so application code never depends on Boost parser
types.

## When To Use

- Build command-line flags from a `config::component_registry`.
- Merge CLI values with defaults and YAML/JSON/env config documents.
- Keep application plugins independent from CLI parser implementation.

## When Not To Use

- Do not use this library inside `fcl_app` core. `fcl_app` consumes config
  documents and descriptors only.
- Do not expose backend parser result maps in public APIs.
- Do not use argv for secrets unless a consuming CLI explicitly accepts the
  risk; prefer stdin or files for secret material.

## Public Module

- `fcl.program_options`

Target: `fcl_program_options`.

Dependencies: `fcl_config`, `fcl_schema`, private Boost.Program_options.

## Examples

### Parse CLI Into A Config Document

```cpp
import fcl.config;
import fcl.program_options;

auto registry = fcl::config::component_registry{};
registry.add(fcl::config::describe_component<http_config>("http"));

const char* argv[] = {
   "tool",
   "--http.bind-port=9090",
   "--http.tls-enabled=false",
};

auto parsed = fcl::program_options::parse(3, argv, registry);
if (!parsed.ok()) {
   report_diagnostics(parsed.diagnostics);
} else {
   auto decoded = fcl::config::decode<http_config>(parsed.document, "http");
   if (!decoded.ok()) {
      report_diagnostics(decoded.diagnostics.entries);
   }
}
```

### Generate Help Text

```cpp
import fcl.program_options;

auto text = fcl::program_options::help(registry, "FCL options");
```

### Merge With File Config

```cpp
import fcl.config;

if (!parsed.ok()) {
   report_diagnostics(parsed.diagnostics);
} else {
   auto effective = fcl::config::merge({
      fcl::config::defaults_for<http_config>("http"),
      yaml_document,
      dotenv_document,
      process_env_document,
      parsed.document,
   });
}
```

### Flat Root Flags

An empty component section maps fields directly to flag names. This keeps
bootstrap flags such as `--log-level` flat instead of forcing a synthetic
section.

```cpp
auto registry = fcl::config::component_registry{};
registry.add(fcl::config::describe_component<daemon_config>(""));

const char* argv[] = {"daemon", "--log-level=debug"};
auto parsed = fcl::program_options::parse(2, argv, registry);
```

CLI should be the last high-precedence source in a normal daemon bootstrap.
Keep the precedence visible near the program entrypoint so operators can reason
about why a value won.

## Diagnostics

Conversion and parser failures return diagnostics such as
`program_options.convert`; callers can print them through their normal
diagnostic/log pipeline.

```cpp
for (const auto& diagnostic : parsed.diagnostics) {
   std::cerr << diagnostic.path << " [" << diagnostic.code << "] "
             << diagnostic.message << "\n";
}
```

## Risks And Anti-Patterns

- Do not treat CLI parsing as validation success. Decode and inspect diagnostics
  before starting runtime components.
- Do not pass high-value secrets on argv by default; process lists and shell
  history can expose them.
- Do not let plugins own CLI precedence. Plugins publish descriptors; program
  bootstrap composes file/env/CLI documents.

## Typical Mistakes

- Do not let plugins call `parse(argc, argv, ...)` themselves. Plugins publish
  descriptors; the application shell decides which adapters are active.
- Do not encode config source precedence in this library. Use `fcl_config::merge`.
- Do not document aliases that are not present in schema descriptors.
- Do not parse environment variables here. Use `fcl_env` for process env and
  `.env` files.
- Do not pass secrets on argv unless the product explicitly accepts the process
  list/history risk. Prefer config files with permissions, stdin or a product
  secret store.
- Do not bridge YAML `options:` arrays into argv. Parse YAML as config and CLI
  as CLI, then merge documents.

## Tests

`test_fcl_program_options` covers dotted flags, flat root flags, explicit
boolean false, repeated list flags, aliases and conversion errors.
