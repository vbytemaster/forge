# forge_json

`forge_json` is the JSON codec boundary. The public API is `namespace forge::json`;
The parser backend is an internal implementation detail and never leaks into
module interfaces.

## When To Use

- Read/write generic `forge::variant` JSON values.
- Read/write `forge::config::document` for configuration.
- Decode typed Boost.Describe objects through `forge_schema` and get diagnostics.

## When Not To Use

- Do not include or expose backend parser types in public FORGE or application APIs.
- Do not use JSON for binary contract compatibility; use `forge_raw`.
- Do not rely on JSON serialization as redaction. Redact before calling write.

## Public Module

- `forge.json`

Target: `forge_json`.

Dependencies: `forge_config`, `forge_schema`, `forge_variant`; the backend parser is
private.

## Examples

### Generic Value Roundtrip

```cpp
import forge.json;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

auto parsed = forge::json::read_value(R"({"name":"node-a","enabled":true})");
if (!parsed.ok()) {
   report_diagnostics(parsed.diagnostics);
} else {
   const auto& value = parsed.value;
   auto name = value.get_object()["name"].get_string();
   auto out = forge::json::write_value(value, {.pretty = true});
}
```

### Config Document Roundtrip

```cpp
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.json;

auto document = forge::config::document{};
document.set("http.bind-host", "127.0.0.1");
document.set("http.bind-port", 8080);

auto written = forge::json::write_document(document);
if (!written.ok()) {
   report_diagnostics(written.diagnostics);
} else {
   auto parsed = forge::json::read_document(written.text);
   if (!parsed.ok()) {
      report_diagnostics(parsed.diagnostics);
   }
}
```

### Typed Decode With Unknown Field Policy

```cpp
import forge.json;

auto options = forge::json::read_options{};
options.unknown_fields = forge::json::unknown_field_policy::error;

auto parsed = forge::json::read<http_config>(
   R"({"bind-port":9090,"extra":1})",
   options);
if (!parsed.ok()) {
   report_diagnostics(parsed.diagnostics);
}
```

### File Helpers

```cpp
import forge.json;

auto result = forge::json::load_document("config.json");
if (!result.ok()) {
   report_diagnostics(result.diagnostics);
} else {
   auto saved = forge::json::save_document("effective.json", result.value, {.pretty = true});
   if (!saved.ok()) {
      report_diagnostics(saved.diagnostics);
   }
}
```

## Diagnostics

Parser, type and schema errors are mapped into `std::vector<forge::schema::diagnostic>`.
Backend parser errors are normalized at the FORGE boundary. Application code should
print the FORGE diagnostic path, code and message instead of exposing parser
implementation details:

```cpp
#include <iostream>

import forge.json;

auto parsed = forge::json::read_value("{invalid json");
for (const auto& diagnostic : parsed.diagnostics) {
   std::cerr << diagnostic.path << " [" << diagnostic.code << "] "
             << diagnostic.message << "\n";
}
```

## Risks And Anti-Patterns

- Do not sign, hash or authorize JSON text. Formatting, field order and number
  rendering are not a binary protocol contract.
- Do not keep running after `read_*` or `write_*` returns error diagnostics.
  Surface diagnostics and fail before configuring runtime components.
- Do not persist redacted JSON as real config; placeholders would replace
  secrets on the next startup.

## Typical Mistakes

- Do not use the removed legacy JSON facade API.
- Do not assume all numbers are safe as `double`; large integer behavior is
  tested explicitly.
- Do not write secret-bearing config without `forge::config::redact`.

## Tests

`test_forge_json` covers generic values, large integers, config documents, typed
schema reads, malformed input diagnostics and no public backend leakage.
