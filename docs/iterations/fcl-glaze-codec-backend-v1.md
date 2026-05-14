# FCL Glaze Codec Backend v1

## Summary

FCL JSON and YAML now use Glaze as the single codec backend. FCL still owns the public API, schema diagnostics, redaction model, `fcl::variant`, `fcl::config::document`, and Boost.Describe/schema rules. Glaze remains an implementation dependency and must not define the public FCL contract.

## Public Shape

- `import fcl.json;`
- `import fcl.yaml;`
- `fcl::json::read<T>()`, `write<T>()`, `read_value()`, `write_value()`, `read_document()`, `write_document()`, `load_*()`, `save_*()`.
- `fcl::yaml` exposes the same shape for YAML.
- `read_value()` and `write_value()` use `fcl::variant`.
- `read_document()` and `write_document()` use `fcl::config::document`.
- `read<T>()` uses schema rules when a schema is defined, otherwise it falls back to `fcl::from_variant` when available.

## Backend Boundary

- Glaze is discovered through package manager CMake config as `glaze::glaze`.
- No `FetchContent`.
- No fallback to the legacy parser.
- YAML no longer depends on the previous backend.
- Public module files must not mention `glz::`, `glaze/`, or Glaze reflection
  metadata.
- Large integer handling uses Glaze generic JSON with `num_mode::u64`, so unsigned 64-bit values are not silently converted to double.

## Diagnostics

Parser/backend errors are converted to `std::vector<fcl::schema::diagnostic>` at the backend boundary. The diagnostic shape is stable: path, code, severity, message. Backend error types are not exposed.

## Compatibility Notes

This is a deliberate source-breaking pass:

- old JSON class API is removed;
- legacy relaxed parser facade is removed;
- old stringify/deadline helper APIs are removed from JSON;
- value/document/typed codec APIs replace parser-object style calls.

## Checks

```sh
cmake --build build/fcl-glaze-codec-debug -j 1 --target test_fcl_json test_fcl_yaml
ctest --test-dir build/fcl-glaze-codec-debug --output-on-failure -R "^(test_fcl_json|test_fcl_yaml)$" --timeout 120
rg "legacy parser facade|old YAML backend|backend node type" AGENTS.md docs libraries tests CMakeLists.txt
rg "glz::|glaze/" libraries/json/include libraries/yaml/include
```
