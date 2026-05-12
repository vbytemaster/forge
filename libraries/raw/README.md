# fcl_raw

`fcl_raw` owns binary serialization. Its main contract is byte-to-byte
compatibility with retained old FC raw layouts for supported types, while using
modern FCL modules and Boost.Describe for structure traversal.

## When To Use

- You need deterministic binary packing for contracts, hashes, signatures or
  persistent wire formats.
- You need FC-compatible byte layout for retained primitives, containers,
  chrono types, variants, described objects and crypto wrappers.
- You need `datastream` helpers for size calculation and buffer packing.

## When Not To Use

- Do not use raw for human-readable config or diagnostics; use JSON/YAML.
- Do not add ad-hoc per-type binary formats if Boost.Describe order is enough.
- Do not serialize secrets into logs or diagnostics just because raw can pack
  them.

## Public Modules

- `fcl.raw.datastream` — buffer/vector/size-counting streams.
- `fcl.raw.varint` — signed/unsigned variable-width integer wrappers.
- `fcl.raw.enum_type` — enum support.
- `fcl.raw.raw` — `pack`, `unpack`, `pack_size`.

Target: `fcl_raw`.

Dependencies: `fcl_core`, `fcl_exception`, `fcl_reflect`, `fcl_variant`,
Boost headers and Boost.Multiprecision.

## Examples

### Pack A Described Struct

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <vector>

struct transfer {
   std::uint64_t id = 0;
   std::uint32_t amount = 0;
};

BOOST_DESCRIBE_STRUCT(transfer, (), (id, amount))

import fcl.raw.datastream;
import fcl.raw.raw;

auto bytes = std::vector<char>{};
bytes.resize(fcl::raw::pack_size(transfer{.id = 7, .amount = 42}));
auto stream = fcl::datastream<char*>{bytes.data(), bytes.size()};
fcl::raw::pack(stream, transfer{.id = 7, .amount = 42});
```

### Calculate Size Before Writing

```cpp
import fcl.raw.datastream;
import fcl.raw.raw;

auto value = std::string{"hello"};
auto size_stream = fcl::datastream<size_t>{};
fcl::raw::pack(size_stream, value);
auto size = size_stream.tellp();
```

### Chrono Wire Compatibility

```cpp
import fcl.raw.raw;

auto time = std::chrono::sys_seconds{std::chrono::seconds{1}};
fcl::raw::pack(stream, time); // old FC time_point_sec: uint32 seconds
```

## Compatibility Rules

- Described member order is wire order. Changing `BOOST_DESCRIBE_*` order is a
  breaking binary change.
- `sys_time<microseconds>` packs as old FC `time_point` (`uint64` microseconds).
- `sys_seconds` packs as old FC `time_point_sec` (`uint32` seconds).
- `std::chrono::microseconds` packs as old FC microseconds (`uint64` bit layout).

## Typical Mistakes

- Do not put `fcl::raw` overloads in `core`.
- Do not use filesystem path serialization as a product policy boundary.
- Do not catch raw bounds failures by parsing `what()`; errors are standard
  exceptions such as `std::out_of_range`.

## Tests

`tests/raw` contains golden byte tests for strings, described structs/enums,
derived types, chrono values, dynamic bitsets and common containers.
