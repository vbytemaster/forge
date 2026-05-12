# fcl_json

`fcl_json` is the JSON codec boundary. The public API is `namespace fcl::json`;
Glaze is an internal backend and never leaks into module interfaces.

## When To Use

- Read/write generic `fcl::variant` JSON values.
- Read/write `fcl::config::document` for configuration.
- Decode typed Boost.Describe objects through `fcl_schema` and get diagnostics.

## When Not To Use

- Do not include or expose `glz::*` types in public FCL or product APIs.
- Do not use JSON for binary contract compatibility; use `fcl_raw`.
- Do not rely on JSON serialization as redaction. Redact before calling write.

## Public Module

- `fcl.json`

Target: `fcl_json`.

Dependencies: `fcl_config`, `fcl_schema`, `fcl_variant`; Glaze is private.

## Examples

### Generic Value Roundtrip

```cpp
import fcl.json;
import fcl.variant;

auto parsed = fcl::json::read_value(R"({"name":"node-a","enabled":true})");
if (parsed.ok()) {
   auto name = parsed.value.get_object()["name"].get_string();
   auto out = fcl::json::write_value(parsed.value, {.pretty = true});
}
```

### Config Document Roundtrip

```cpp
import fcl.config;
import fcl.json;

auto document = fcl::config::document{};
document.set("http.bind-host", "127.0.0.1");
document.set("http.bind-port", 8080);

auto written = fcl::json::write_document(document);
auto parsed = fcl::json::read_document(written.text);
```

### Typed Decode With Unknown Field Policy

```cpp
import fcl.json;

auto options = fcl::json::read_options{};
options.unknown_fields = fcl::json::unknown_field_policy::error;

auto parsed = fcl::json::read<http_config>(
   R"({"bind-port":9090,"extra":1})",
   options);
```

### File Helpers

```cpp
import fcl.json;

auto result = fcl::json::load_document("config.json");
auto saved = fcl::json::save_document("effective.json", result.value, {.pretty = true});
```

## Diagnostics

Parser, type and schema errors are mapped into `std::vector<fcl::schema::diagnostic>`.
Glaze error types and messages are normalized at the backend boundary.

## Typical Mistakes

- Do not use the removed `class json` API.
- Do not assume all numbers are safe as `double`; large integer behavior is
  tested explicitly.
- Do not write secret-bearing config without `fcl::config::redact`.

## Tests

`test_fcl_json` covers generic values, large integers, config documents, typed
schema reads, malformed input diagnostics and no public backend leakage.
