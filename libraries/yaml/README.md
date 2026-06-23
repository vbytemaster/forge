# forge_yaml

`forge_yaml` is the YAML codec boundary. Its API mirrors `forge_json` so callers can
switch between JSON and YAML without learning two different public models. The
parser backend is an implementation detail.

## When To Use

- Load human-authored config files into `forge::config::document`.
- Read/write YAML as `forge::variant` for diagnostics or tooling.
- Decode typed Boost.Describe objects through `forge_schema`.

## When Not To Use

- Do not expose backend parser nodes in public APIs.
- Do not use YAML for machine-stable binary compatibility.
- Do not rely on YAML formatting for secret safety; redact first.

## Public Module

- `forge.yaml`

Target: `forge_yaml`.

Dependencies: `forge_config`, `forge_schema`, `forge_variant`; the parser backend is
private.

## Examples

### Read A Config Document

```cpp
import forge.yaml;

auto parsed = forge::yaml::read_document(
   "http:\n"
   "  bind-host: 127.0.0.1\n"
   "  bind-port: 8080\n");

if (!parsed.ok()) {
   // inspect parsed.diagnostics
}
```

### Decode A Typed Object

```cpp
import forge.yaml;

auto config = forge::yaml::read<http_config>(
   "bind-port: 9090\n"
   "tls-enabled: false\n");
if (!config.ok()) {
   report_diagnostics(config.diagnostics);
}
```

### Write A Redacted Effective Config

```cpp
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.yaml;

auto safe = forge::config::redact(document, registry);
auto output = forge::yaml::write_document(safe);
if (!output.ok()) {
   report_diagnostics(output.diagnostics);
}
```

Write redacted documents for diagnostics and `--print-effective-config`.
Persisting the redacted document back as the real config would replace secrets
with placeholders and break the next startup.

### Value Mode For Tools

```cpp
import forge.yaml;

auto value = forge::yaml::read_value("items:\n  - alpha\n  - beta\n");
if (!value.ok()) {
   report_diagnostics(value.diagnostics);
} else {
   auto compact = forge::yaml::write_value(value.value, {.flow_style = true});
   if (!compact.ok()) {
      report_diagnostics(compact.diagnostics);
   }
}
```

## Diagnostics

YAML parse/type/schema failures become `forge::schema::diagnostic` entries. Source
name and path metadata should be set by callers via `read_options` where useful.

```cpp
for (const auto& diagnostic : parsed.diagnostics) {
   std::cerr << diagnostic.path << " [" << diagnostic.code << "] "
             << diagnostic.message << "\n";
}
```

## Risks And Anti-Patterns

- Do not load YAML inside plugins. The application/daemon bootstrap loads
  source documents and passes typed component views.
- Do not use YAML as a protocol byte representation for signatures or hashes.
  Use `forge::raw::pack` for deterministic binary contracts.
- Do not continue with `.value` after failed parse diagnostics. Bad config must
  stop before ports or plugins initialize.

## Typical Mistakes

- Do not preserve comments or formatting assumptions in v1; this is a codec, not
  a YAML editor.
- Do not put YAML-specific policy into `forge_config`.
- Do not silently ignore unknown fields in production tools unless the caller
  explicitly uses `unknown_field_policy::ignore`.

## Tests

`test_forge_yaml` covers scalar/list/map roundtrip, config document behavior,
typed schema reads and malformed YAML diagnostics.
