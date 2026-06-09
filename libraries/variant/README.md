# fcl_variant

`fcl_variant` provides the FCL dynamic value model: scalar values, arrays,
objects, blobs, described-type conversion, `static_variant` and dynamic bitsets.
It is the bridge between typed C++ values and generic codec/config/log shapes.

## When To Use

- A layer needs JSON-like value trees without depending on a concrete parser.
- You need `to_variant/from_variant` for described structs, enums or containers.
- You need FC-like `static_variant` behavior for retained wire compatibility.

## When Not To Use

- Do not use `variant` as an internal data model when a typed struct is known.
- Do not put validation or required-field policy here; use `fcl_schema`.
- Do not put binary serialization here; use `fcl_raw`.

## Public Modules

- `fcl.variant.value` — `variant`, `variant_object`, `mutable_variant_object`.
- `fcl.variant.conversion` — scalar/string/blob conversions.
- `fcl.variant.containers` — STL container conversions.
- `fcl.variant.described` — Boost.Describe object/enum mapping.
- `fcl.variant.chrono` — std chrono ISO conversion.
- `fcl.variant.multiprecision` — Boost multiprecision conversions.
- `fcl.variant.format` — display helpers.
- `fcl.variant.static_variant` — FC-style static variant.
- `fcl.variant.dynamic_bitset`, `fcl.variant.variant_dynamic_bitset`.
- `fcl.variant` — aggregate import.

Target: `fcl_variant`.

Dependencies: `fcl_core`, `fcl_reflect`, Boost headers, Boost.MultiIndex and
Boost.Multiprecision.

## Examples

### Build A Value Object

```cpp
import fcl.variant;

auto object = fcl::mutable_variant_object{};
object("name", "node-a")("enabled", true)("retries", 3);

fcl::variant value{object};
auto enabled = value.get_object()["enabled"].as_bool();
```

### Convert A Described Type

```cpp
#include <boost/describe.hpp>

#include <string>

struct profile {
   std::string name;
   bool enabled = false;
};

BOOST_DESCRIBE_STRUCT(profile, (), (name, enabled))

import fcl.variant.described;

auto source = profile{.name = "dev", .enabled = true};
auto value = fcl::variant{source};
auto restored = value.as<profile>();
```

### Chrono Values Use ISO Text

```cpp
import fcl.variant.chrono;

auto now = std::chrono::sys_time<std::chrono::microseconds>{std::chrono::microseconds{1}};
auto value = fcl::variant{now};
auto restored = value.as<std::chrono::sys_time<std::chrono::microseconds>>();
```

## Security Notes

`variant` does not know what is secret. Redaction belongs to config/schema/log/UI
layers before rendering or serialization.

## Risks And Anti-Patterns

- Do not use `variant` as the primary model for application config when a typed
  Boost.Describe struct exists.
- Do not rely on dynamic field lookup for protocol compatibility. Raw contracts
  need typed DTOs and stable field order.
- Do not render arbitrary variants to logs before redaction. The value layer has
  no schema metadata.

## Typical Mistakes

- Do not move `variant` into `core`; many upper layers depend on it, but `core`
  must stay lower than dynamic value conversion.
- Do not assume unknown object fields are validation errors here. Schema/config
  decide that policy.
- `dynamic_bitset` is intentionally a `boost::dynamic_bitset<std::uint8_t>`
  alias because the C++ standard library has no equivalent runtime-size bitset
  with the needed block behavior.

## Tests

`tests/variant` covers described roundtrip, missing/unknown field behavior,
bad enum values, chrono ISO conversion, `static_variant`, blob compatibility and
dynamic bitsets.
