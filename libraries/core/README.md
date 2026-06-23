# forge_core

`forge_core` — самый нижний слой FORGE: маленькие value/helpers, которые можно
использовать почти везде без риска подтянуть serialization, JSON, crypto,
logging or network dependencies.

## When To Use

- Нужно разобрать/сформатировать `std::chrono` timestamps в FC-compatible форме.
- Нужны безопасные string/UTF-8 helpers, `uint128`, type names or version metadata.
- Нужно написать библиотеку верхнего уровня и не создать обратную зависимость на
  `variant`, `raw`, `json`, `log` or `crypto`.

## When Not To Use

- Для файловых путей: используйте `std::filesystem::path`.
- Для binary serialization: это `forge_raw`, не `core`.
- Для динамических JSON-like values: это `forge_variant`.
- Для domain-specific helpers consuming applications should own themselves.

## Public Modules

- `forge.core.chrono` — ISO helpers and FC wire conversions for `std::chrono`.
- `forge.core.string` — numeric parsing and escaped string formatting.
- `forge.core.type_name` — diagnostic-friendly type names.
- `forge.core.uint128` — retained 128-bit value support.
- `forge.core.utf8` — UTF-8 validation/cleanup wrappers.
- `forge.core.utility` — small utility primitives such as `yield_function_t`.
- `forge.core.version`, `forge.core.git_revision` — build/version metadata.

Target: `forge_core`.

Owned dependencies: minimal Boost implementation details only. `forge_core` must
not import or link `forge_raw`, `forge_variant`, `forge_json`, `forge_log`, `forge_crypto`
or network targets.

## Examples

### Parse Numbers And Escape Strings

```cpp
import forge.core.string;

auto value = forge::to_uint64("18446744073709551615");
auto printable = forge::escape_str("line\nbreak", 64);
```

### Use Std Chrono With FC Wire Helpers

```cpp
import forge.core.chrono;

auto time = forge::chrono::from_iso_time_point("2026-05-12T08:30:00.000001");
auto wire = forge::chrono::to_fc_time_point_wire(time); // uint64 microseconds
auto restored = forge::chrono::from_fc_time_point_wire(wire);
auto text = forge::chrono::to_iso_string(restored);
```

### Work With Seconds-Level Contract Times

```cpp
import forge.core.chrono;

auto deadline = std::chrono::sys_seconds{std::chrono::seconds{1}};
auto wire = forge::chrono::to_fc_time_point_sec_wire(deadline); // uint32 seconds
auto decoded = forge::chrono::from_fc_time_point_sec_wire(wire);
```

## Risks And Anti-Patterns

- Do not use core helpers to hide application policy. If a rule depends on files,
  network, credentials or daemon layout, it belongs above `forge_core`.
- Do not reintroduce global clocks or mock-time state. Tests should pass
  explicit `std::chrono` values.
- Do not add convenience imports from upper libraries. A small dependency in
  `core` becomes a dependency of everything.

## Typical Mistakes

- Do not reintroduce `forge::time_point` aliases. Public time API is `std::chrono`.
- Do not put raw overloads or `to_variant/from_variant` here. They belong to
  `forge_raw` and `forge_variant`.
- Do not hide filesystem policy in `core`; consumers decide their own path rules.

## Tests

`tests/core` covers chrono ISO/wire compatibility, string escaping and retained
low-level behavior. Any new `core` API must prove it does not depend on upper
FORGE domains.
