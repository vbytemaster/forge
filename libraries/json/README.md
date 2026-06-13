# fcl_json

`fcl_json` is the JSON codec boundary. The public API is `namespace fcl::json`;
The parser backend is an internal implementation detail and never leaks into
module interfaces.

## When To Use

- Read/write generic `fcl::variant` JSON values.
- Read/write `fcl::config::document` for configuration.
- Decode typed Boost.Describe objects through `fcl_schema` and get diagnostics.

## When Not To Use

- Do not include or expose backend parser types in public FCL or application APIs.
- Do not use JSON for binary contract compatibility; use `fcl_raw`.
- Do not rely on JSON serialization as redaction. Redact before calling write.

## Public Module

- `fcl.json`

Target: `fcl_json`.

Dependencies: `fcl_config`, `fcl_schema`, `fcl_variant`; the backend parser is
private.

## Examples

### Generic Value Roundtrip

```cpp
import fcl.json;
import fcl.variant.exceptions;
import fcl.variant.value;
import fcl.variant.conversion;
import fcl.variant.containers;
import fcl.variant.chrono;
import fcl.variant.multiprecision;
import fcl.variant.format;
import fcl.variant.described;

auto parsed = fcl::json::read_value(R"({"name":"node-a","enabled":true})");
if (!parsed.ok()) {
   report_diagnostics(parsed.diagnostics);
} else {
   const auto& value = parsed.value;
   auto name = value.get_object()["name"].get_string();
   auto out = fcl::json::write_value(value, {.pretty = true});
}
```

### Config Document Roundtrip

```cpp
import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;
import fcl.json;

auto document = fcl::config::document{};
document.set("http.bind-host", "127.0.0.1");
document.set("http.bind-port", 8080);

auto written = fcl::json::write_document(document);
if (!written.ok()) {
   report_diagnostics(written.diagnostics);
} else {
   auto parsed = fcl::json::read_document(written.text);
   if (!parsed.ok()) {
      report_diagnostics(parsed.diagnostics);
   }
}
```

### Typed Decode With Unknown Field Policy

```cpp
import fcl.json;

auto options = fcl::json::read_options{};
options.unknown_fields = fcl::json::unknown_field_policy::error;

auto parsed = fcl::json::read<http_config>(
   R"({"bind-port":9090,"extra":1})",
   options);
if (!parsed.ok()) {
   report_diagnostics(parsed.diagnostics);
}
```

### File Helpers

```cpp
import fcl.json;

auto result = fcl::json::load_document("config.json");
if (!result.ok()) {
   report_diagnostics(result.diagnostics);
} else {
   auto saved = fcl::json::save_document("effective.json", result.value, {.pretty = true});
   if (!saved.ok()) {
      report_diagnostics(saved.diagnostics);
   }
}
```

## Diagnostics

Parser, type and schema errors are mapped into `std::vector<fcl::schema::diagnostic>`.
Backend parser errors are normalized at the FCL boundary. Application code should
print the FCL diagnostic path, code and message instead of exposing parser
implementation details:

```cpp
#include <iostream>

import fcl.json;

auto parsed = fcl::json::read_value("{invalid json");
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
- Do not write secret-bearing config without `fcl::config::redact`.

## Tests

`test_fcl_json` covers generic values, large integers, config documents, typed
schema reads, malformed input diagnostics and no public backend leakage.
