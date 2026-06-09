# fcl_reflect

`fcl_reflect` is a thin Boost.Describe utility layer. It centralizes described
type detection, member traversal and enum conversion so `raw`, `variant`,
`schema` and other libraries do not each write their own reflection boilerplate.

## When To Use

- You need to iterate Boost.Describe members in stable order.
- You need base-first traversal for described derived types.
- You need enum name/int conversion for diagnostics or codecs.

## When Not To Use

- Do not put `to_variant/from_variant` here. Described value mapping belongs to
  `fcl_variant`.
- Do not put validation rules here. Validation metadata belongs to `fcl_schema`.
- Do not put application schema or config defaults here.

## Public Modules

- `fcl.reflect.reflect`

Target: `fcl_reflect`.

Dependencies: `fcl_core` and Boost.Describe headers. `fcl_reflect` must not link
or import `fcl_variant`.

## Examples

### Describe A Struct Once

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <string>

struct endpoint_config {
   std::string host;
   std::uint16_t port = 0;
};

BOOST_DESCRIBE_STRUCT(endpoint_config, (), (host, port))
```

The same member order is then consumed by `fcl_raw`, `fcl_variant` and
`fcl_schema` helpers.

### Convert Described Enum Names

```cpp
#include <boost/describe.hpp>

enum class mode { active, passive };
BOOST_DESCRIBE_ENUM(mode, active, passive)

import fcl.reflect.reflect;

auto text = fcl::reflect::enum_to_string(mode::active);
auto parsed = mode{};
auto ok = fcl::reflect::enum_from_string("passive", parsed);
```

## Compatibility Rule

For types that replace old `FC_REFLECT(TYPE, (a)(b)(c))`, the new
`BOOST_DESCRIBE_*` member list must keep the same order. `fcl_raw` uses that
order for byte-compatible packing.

## Risks And Anti-Patterns

- Do not treat reflection metadata as business validation. It describes shape
  and order; schema/application layers validate meaning.
- Do not reorder described members as a cleanup unless every raw/wire consumer
  gets a compatibility migration.
- Do not add application-specific reflection macros here. FCL stays neutral and
  Boost.Describe remains the explicit source of member order.

## Typical Mistakes

- Do not add `FCL_DESCRIBE_*` wrappers casually. The canonical spelling is
  Boost.Describe until a separate compatibility decision changes it.
- Do not import `fcl.variant` from this library to "make it convenient".

## Tests

Reflect behavior is mostly exercised through `test_fcl_raw`,
`test_fcl_variant`, `test_fcl_schema` and `test_fcl_config`, where member order
and enum mapping are observable.
