# JSON + YAML Codecs

`fcl_json` and `fcl_yaml` expose a symmetric typed codec API over a Glaze
backend. FCL owns the public API, value/document model, schema diagnostics and
redaction boundary.

Local guides:

- [JSON README](../../libraries/json/README.md)
- [YAML README](../../libraries/yaml/README.md)

## Задача

The old parser facade shape made JSON look like a namespace while being a fake
class with static methods. YAML had a narrower config-only surface. FCL now uses
one modern API shape for both formats:

- `read_value` / `write_value` for generic `fcl::variant`;
- `read_document` / `write_document` for `fcl::config::document`;
- `read<T>` / `write<T>` for typed described objects;
- `load_*` / `save_*` for filesystem paths.

## Data Flow

```text
JSON/YAML text
  -> Glaze backend parse
  -> fcl::variant or fcl::config::document
  -> optional fcl_schema decode/validation
  -> read_result<T> with diagnostics
```

Typed reads do not expose Glaze reflection as the canonical FCL model. FCL uses
Boost.Describe plus `fcl_schema` rules for public typed behavior.

## Diagnostics

All parser/type/schema errors are converted into
`std::vector<fcl::schema::diagnostic>`. Diagnostics carry path, code, severity
and message. Backend parser error types do not cross the module boundary.

## Redaction

Codecs serialize the value or document they receive. They do not guess which
fields are secret. Callers must use `fcl_config::redact` or another explicit
redaction step before writing logs, diagnostics or operator-visible output.

## Unknown Fields

Typed reads support `unknown_field_policy`:

- `ignore` — no diagnostics for unknown fields;
- `warn` — diagnostics with warning severity;
- `error` — diagnostics become read failure.

This policy only has meaning when schema rules can provide known field names.

## Backend Boundary

- `glz::*` does not appear in public `.cppm`.
- `YAML::Node` is not used or exposed by FCL YAML after the Glaze migration.
- Large integer behavior is tested to prevent silent double conversion.
- Pretty/flow-style output options are codec concerns; schema and config stay
  parser-neutral.

## Verification

- `test_fcl_json`: generic value roundtrip, large integers, config document
  roundtrip, typed schema read and malformed input diagnostics.
- `test_fcl_yaml`: scalar/list/map roundtrip, config document roundtrip, typed
  schema read and malformed YAML diagnostics.
