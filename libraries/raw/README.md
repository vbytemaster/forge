# forge_raw

`forge_raw` owns binary serialization. Its main contract is byte-to-byte
compatibility with retained old FC raw layouts for supported types, while using
modern FORGE modules and Boost.Describe for structure traversal.

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

- `forge.raw.datastream` — buffer/vector/size-counting streams.
- `forge.raw.varint` — signed/unsigned variable-width integer wrappers.
- `forge.raw.enum_type` — enum support.
- `forge.raw.raw` — `pack`, `unpack`, `pack_size`.
- `forge/raw/serialization.hpp` — macro-only explicit-instantiation helpers for
  application/domain DTOs.

Target: `forge_raw`.

Dependencies: `forge_core`, `forge_exceptions`, `forge_reflect`, `forge_variant`,
Boost headers and Boost.Multiprecision.

## Examples

### Pack A Described Struct

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <vector>

import forge.raw.datastream;
import forge.raw.raw;

struct transfer {
   std::uint64_t id = 0;
   std::uint32_t amount = 0;
};

BOOST_DESCRIBE_STRUCT(transfer, (), (id, amount))

auto bytes = std::vector<char>{};
bytes.resize(forge::raw::pack_size(transfer{.id = 7, .amount = 42}));
auto stream = forge::datastream<char*>{bytes.data(), bytes.size()};
forge::raw::pack(stream, transfer{.id = 7, .amount = 42});
```

### Use Raw Bytes As The Hash/Signature Contract

When an application signs or hashes a C++ structure, the signed bytes must come from
the same `forge::raw::pack` path that the verifier uses. Do not rebuild bytes with
string concatenation, JSON or hand-written field loops.

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <string>

import forge.crypto.asymmetric;
import forge.crypto.sha256;
import forge.raw.raw;

struct signed_command {
   std::uint64_t account = 0;
   std::uint64_t sequence = 0;
   std::string command;

   [[nodiscard]] forge::crypto::bytes signing_bytes() const;
};

BOOST_DESCRIBE_STRUCT(signed_command, (), (account, sequence, command))

inline forge::crypto::bytes signed_command::signing_bytes() const {
   auto bytes = forge::crypto::bytes{};
   forge::raw::pack(bytes, *this);
   return bytes;
}

auto command = signed_command{
   .account = 42,
   .sequence = 11,
   .command = "rotate-key",
};

auto private_key = forge::crypto::asymmetric::private_key::generate();
auto expected_public_key = private_key.get_public_key();

auto message = command.signing_bytes();
auto signature = private_key.sign(message);

auto verified = expected_public_key.verify(message, signature);
```

Store golden raw bytes for protocol DTOs in tests. That catches accidental
member reordering before it becomes an interoperability break.

Avoid shortcuts in signing code:

- Do not sign JSON/YAML text, `to_string()` output or manually concatenated
  fields.
- Do not materialize a temporary byte buffer only to hash it when the sink
  accepts `forge::raw::pack` directly.
- Do not treat a recoverable signature as authorized until the recovered public
  key equals the expected signer.

### Calculate Size Before Writing

```cpp
import forge.raw.datastream;
import forge.raw.raw;

auto value = std::string{"hello"};
auto size_stream = forge::datastream<size_t>{};
forge::raw::pack(size_stream, value);
auto size = size_stream.tellp();
```

### Chrono Wire Compatibility

```cpp
import forge.raw.raw;

auto time = std::chrono::sys_seconds{std::chrono::seconds{1}};
forge::raw::pack(stream, time); // old FC time_point_sec: uint32 seconds
```

### Declare Explicit Serialization Instantiations

Use the macro-only header when an application wants one `.cpp` file to own template
instantiations for a frequently used DTO, while other translation units only see
`extern template` declarations.

```cpp
#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <string>

import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

struct action_payload {
   std::uint64_t id = 0;
   std::string actor;
};

BOOST_DESCRIBE_STRUCT(action_payload, (), (id, actor))

FORGE_DECLARE_SERIALIZATION(action_payload)
```

Then place the implementation macro in exactly one module implementation unit
or `.cpp` file:

```cpp
#include <forge/raw/serialization.hpp>

import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

FORGE_IMPLEMENT_SERIALIZATION(action_payload)
```

`FORGE_DECLARE_SERIALIZATION_PACK` and `FORGE_IMPLEMENT_SERIALIZATION_PACK` cover
`datastream<size_t>`, `datastream<char*>`, `datastream<const char*>` and
`sha256::encoder`. `FORGE_DECLARE_SERIALIZATION_VARIANT` and
`FORGE_IMPLEMENT_SERIALIZATION_VARIANT` cover `to_variant/from_variant`.

## Compatibility Rules

- Described member order is wire order. Changing `BOOST_DESCRIBE_*` order is a
  breaking binary change.
- `sys_time<microseconds>` packs as old FC `time_point` (`uint64` microseconds).
- `sys_seconds` packs as old FC `time_point_sec` (`uint32` seconds).
- `std::chrono::microseconds` packs as old FC microseconds (`uint64` bit layout).

## Runtime Risks And Anti-Patterns

- Do not pack runtime resources such as file handles, sockets, executors or
  pointers. Raw is for value DTOs with deterministic ownership.
- Do not use raw bytes as diagnostics output. Convert to JSON/YAML or render
  explicit safe fields after redaction.
- Do not continue after `std::out_of_range` from unpack as if the stream were
  partially valid. Treat it as a malformed input boundary and fail the operation.
- Do not add raw overloads in unrelated libraries to “make it compile”. The
  owning domain should describe the value type or provide a narrowly reviewed
  compatibility overload.

## Typical Mistakes

- Do not put `forge::raw` overloads in `core`.
- Do not use filesystem path serialization as an application policy boundary.
- Do not catch raw bounds failures by parsing `what()`; errors are standard
  exceptions such as `std::out_of_range`.

## Tests

`tests/raw` contains golden byte tests for strings, described structs/enums,
derived types, chrono values, dynamic bitsets and common containers.
