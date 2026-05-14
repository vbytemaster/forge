# fcl_core

`fcl_core` — самый нижний слой FCL: маленькие value/helpers, которые можно
использовать почти везде без риска подтянуть serialization, JSON, crypto,
logging or network dependencies.

## When To Use

- Нужно разобрать/сформатировать `std::chrono` timestamps в FC-compatible форме.
- Нужны безопасные string/UTF-8 helpers, `uint128`, type names or version metadata.
- Нужно написать библиотеку верхнего уровня и не создать обратную зависимость на
  `variant`, `raw`, `json`, `log` or `crypto`.

## When Not To Use

- Для файловых путей: используйте `std::filesystem::path`.
- Для binary serialization: это `fcl_raw`, не `core`.
- Для динамических JSON-like values: это `fcl_variant`.
- Для domain-specific helpers consuming applications should own themselves.

## Public Modules

- `fcl.core.chrono` — ISO helpers and FC wire conversions for `std::chrono`.
- `fcl.core.string` — numeric parsing and escaped string formatting.
- `fcl.core.type_name` — diagnostic-friendly type names.
- `fcl.core.uint128` — retained 128-bit value support.
- `fcl.core.utf8` — UTF-8 validation/cleanup wrappers.
- `fcl.core.utility` — small utility primitives such as `yield_function_t`.
- `fcl.core.version`, `fcl.core.git_revision` — build/version metadata.

Target: `fcl_core`.

Owned dependencies: minimal Boost implementation details only. `fcl_core` must
not import or link `fcl_raw`, `fcl_variant`, `fcl_json`, `fcl_log`, `fcl_crypto`
or network targets.

## Examples

### Parse Numbers And Escape Strings

```cpp
import fcl.core.string;

auto value = fcl::to_uint64("18446744073709551615");
auto printable = fcl::escape_str("line\nbreak", 64);
```

### Use Std Chrono With FC Wire Helpers

```cpp
import fcl.core.chrono;

auto time = fcl::chrono::from_iso_time_point("2026-05-12T08:30:00.000001");
auto wire = fcl::chrono::to_fc_time_point_wire(time); // uint64 microseconds
auto restored = fcl::chrono::from_fc_time_point_wire(wire);
auto text = fcl::chrono::to_iso_string(restored);
```

### Work With Seconds-Level Contract Times

```cpp
import fcl.core.chrono;

auto deadline = std::chrono::sys_seconds{std::chrono::seconds{1}};
auto wire = fcl::chrono::to_fc_time_point_sec_wire(deadline); // uint32 seconds
auto decoded = fcl::chrono::from_fc_time_point_sec_wire(wire);
```

## Risks And Anti-Patterns

- Do not use core helpers to hide product policy. If a rule depends on files,
  network, credentials or daemon layout, it belongs above `fcl_core`.
- Do not reintroduce global clocks or mock-time state. Tests should pass
  explicit `std::chrono` values.
- Do not add convenience imports from upper libraries. A small dependency in
  `core` becomes a dependency of everything.

## Typical Mistakes

- Do not reintroduce `fcl::time_point` aliases. Public time API is `std::chrono`.
- Do not put raw overloads or `to_variant/from_variant` here. They belong to
  `fcl_raw` and `fcl_variant`.
- Do not hide filesystem policy in `core`; consumers decide their own path rules.

## Tests

`tests/core` covers chrono ISO/wire compatibility, string escaping and retained
low-level behavior. Any new `core` API must prove it does not depend on upper
FCL domains.
