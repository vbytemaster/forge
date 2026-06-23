# forge_env

`forge_env` converts process environment variables and explicit `.env` files into
`forge::config::document`. It is a config source adapter, like
`forge_program_options` for CLI flags: plugins and applications publish schema
descriptors, `forge_env` maps prefixed variables to canonical config keys, and
the caller decides merge precedence.

## When To Use

- CI/CD, containers, systemd, launchd or Kubernetes need config through
  environment variables.
- A generated workspace should include a `.env.example` without writing real
  secrets.
- Tests need deterministic config input without mutating global process env.
- You want schema-aware diagnostics for aliases, unknown names, deprecated
  fields and conversion errors.

## When Not To Use

- Do not hide environment loading inside `forge_app`; app receives a ready
  `config::document`.
- Do not use env vars as the only high-value secret transport. Environment
  values may leak through process inspection, CI logs or crash dumps.
- Do not rely on implicit `.env` discovery from the current directory. The
  caller must pass an explicit path.
- Do not put application bootstrap names such as workspace/config-file discovery in
  FORGE. That belongs to the consuming application.

## Public Module

- `forge.env`

Target: `forge_env`.

Dependencies: `forge_config`, `forge_schema`.

No dependency on `forge_app`, Glaze, Boost.Program_options, OpenSSL, ngtcp2,
Notcurses or crypto backends.

## Naming

Variables are generated from the config registry:

```text
<PREFIX>_<SECTION>_<FIELD>
```

`-`, `.`, `/` and repeated separators become `_`, then names are uppercased:

```text
http.bind-port    -> FORGE_APP_HTTP_BIND_PORT
http.tls-enabled  -> FORGE_APP_HTTP_TLS_ENABLED
log-level         -> FORGE_APP_LOG_LEVEL
```

Aliases are accepted when enabled, but values are written to the canonical
dotted config path.

## Examples

### Read A `.env` File

```cpp
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.env;

auto registry = forge::config::component_registry{};
registry.add(forge::config::describe_component<http_config>("http"));

auto dotenv = forge::env::load_document(
   workspace / ".env",
   registry,
   forge::env::read_options{
      .prefix = "FORGE_APP",
      .source_name = "workspace/.env",
   });

if (!dotenv.ok()) {
   report_diagnostics(dotenv.diagnostics);
}
```

### Merge With YAML And CLI

`forge_env` does not own precedence. Keep it explicit at the composition layer:

```cpp
auto file = forge::yaml::load_document(workspace / "config.yml");
auto dotenv = forge::env::load_document(workspace / ".env", registry, {.prefix = "FORGE_APP"});
auto env = forge::env::read_process_document(registry, {.prefix = "FORGE_APP"});
auto cli = forge::program_options::parse(argc, argv, registry);

if (!file.ok()) {
   report_diagnostics(file.diagnostics);
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

if (file.ok() && dotenv.ok() && env.ok() && cli.ok()) {
   auto input = forge::config::merge({
      file.value,
      dotenv.value,
      env.value,
      cli.document,
   });

   app.configure(input);
}
```

Recommended application precedence is:

```text
schema defaults < config file < .env < process env < CLI
```

### Typed Single-Section Read

For a small tool or test, the typed helper can build the registry from one
described config type:

```cpp
auto parsed = forge::env::read<http_config>(
   "FORGE_APP_HTTP_BIND_PORT=9090\n",
   "http",
   {.prefix = "FORGE_APP"});

if (parsed.ok()) {
   start_http(parsed.value);
}
```

### Deterministic Environment Tests

Avoid mutating global process environment in tests. Use `read_variables()`:

```cpp
auto vars = std::vector<forge::env::environment_variable>{
   {.name = "FORGE_APP_HTTP_BIND_PORT", .value = "9090"},
};

auto parsed = forge::env::read_variables(vars, registry, {.prefix = "FORGE_APP"});
```

### Generate `.env.example`

```cpp
auto example = forge::env::write_example(registry, {.prefix = "FORGE_APP"});
if (example.ok()) {
   forge::env::save_example(workspace / ".env.example", registry, {.prefix = "FORGE_APP"});
}
```

Secret fields are emitted as empty placeholders in examples:

```dotenv
# HTTP bearer token.
# Secret value. Prefer a platform secret manager in production.
FORGE_APP_HTTP_TOKEN=
```

### Write Current Overrides

```cpp
auto redacted = forge::env::write_document(document, registry, {.prefix = "FORGE_APP"});
if (!redacted.ok()) {
   report_diagnostics(redacted.diagnostics);
}
```

Secret values are never written back as real values; they render as
`<redacted>`.

## `.env` Syntax

Supported v1 syntax:

```dotenv
# comment
export FORGE_APP_HTTP_BIND_PORT=9090
FORGE_APP_HTTP_TLS_ENABLED=true
FORGE_APP_HTTP_TOKEN="secret value"
FORGE_APP_HTTP_TAGS=blue,green
```

Supported:

- blank lines and comments;
- optional `export`;
- whitespace around `=`;
- single and double quotes;
- double-quoted escapes: `\n`, `\r`, `\t`, `\\`, `\"`;
- comma-separated `string_list` with `\,` for literal comma.

Not supported in v1:

- variable expansion like `${HOME}`;
- command substitution;
- multiline values;
- implicit parent-directory search;
- JSON object values.

## Diagnostics

Common diagnostic codes:

- `env.parse` — malformed `.env` line.
- `env.duplicate` — duplicate variable, later value wins.
- `env.unknown` — prefixed variable not present in registry.
- `env.alias_conflict` — canonical and alias variables disagree.
- `env.deprecated` — deprecated field was provided.
- `env.convert` — value cannot be converted to schema kind.

Unknown prefixed variables warn by default and can be made fatal with
`unknown_variable_policy::error`.

## Security Notes

- Treat `.env` as local secret material and usually gitignore it.
- Generate `.env.example`, not `.env` with real secrets.
- Redact effective config before logging.
- Prefer secret managers, mounted secret files or stdin for high-value secrets.
- Do not let plugins call `std::getenv()` directly; keep precedence and
  diagnostics centralized.

## Runtime Risks And Anti-Patterns

- Do not mutate global process environment in tests. Use `read_variables()` so
  tests stay deterministic and parallel-safe.
- Do not silently ignore `env.name_conflict`, `env.alias_conflict` or
  `env.convert`; bad environment input must stop startup before ports/plugins
  initialize.
- Do not search parent directories for `.env` files. Implicit discovery can
  bind a program to the wrong workspace or leak local developer settings into
  production.
- Do not write real secret values into generated `.env.example` files or logs.
  Use placeholders/redaction and application-owned secret storage.

## Tests

`test_forge_env` covers env name mapping, flat sections, aliases, dotenv syntax,
source-line diagnostics, injected process env snapshots, conversions, unknowns,
deprecated fields, alias conflicts and secret-safe output.
