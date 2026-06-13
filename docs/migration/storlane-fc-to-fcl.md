# Migration Guide: storlane-fc To FCL

## Status

FCL is a breaking source API. It does not restore `fc::...` source
compatibility. Compatibility is guaranteed only where tests preserve old raw
wire bytes, primarily through `fcl::raw::pack/unpack` for retained types.

## CMake

Prefer leaf targets:

```cmake
find_package(FCL CONFIG REQUIRED COMPONENTS raw crypto app log)

target_link_libraries(my_program PRIVATE
   FCL::fcl_raw
   FCL::fcl_crypto
   FCL::fcl_app
   FCL::fcl_log
)
```

Use `find_package(FCL CONFIG REQUIRED)` without components only for lightweight
`FCL::fcl_core` consumers. Use `FCL::fcl` only when the consumer intentionally
wants every FCL feature and its transitive dependencies; request `COMPONENTS all`
before linking that aggregate.

## Includes And Modules

Old source-style includes are removed:

```cpp
// old
#include <fc/raw/raw.hpp>

// new
import fcl.raw.raw;
```

Public APIs live in module files under `libraries/<lib>/include/fcl/<lib>`.
Macro-only headers remain textual, for example:

```cpp
#include <fcl/exceptions/macros.hpp>
#include <fcl/log/macros.hpp>
```

## Reflection

Old reflection macros are not preserved:

```cpp
// old
FC_REFLECT(config, (bind_host)(bind_port))

// new
#include <boost/describe.hpp>

BOOST_DESCRIBE_STRUCT(config, (), (bind_host, bind_port))
```

Field order is compatibility-critical. If an old raw layout used
`(a)(b)(c)`, the new `BOOST_DESCRIBE_*` order must stay `a, b, c`.

## Raw Serialization

```cpp
#include <vector>

import fcl.raw.datastream;
import fcl.raw.raw;

auto bytes = std::vector<char>{};
bytes.resize(fcl::raw::pack_size(value));
auto stream = fcl::datastream<char*>{bytes.data(), bytes.size()};
fcl::raw::pack(stream, value);
```

`fcl::raw::pack/unpack` keeps old wire compatibility for covered primitive,
container, variant/static_variant, Boost.Describe object, chrono and crypto
wrapper cases. Deleted old source types have no compatibility guarantee.

## Time

Old time aliases are replaced by `std::chrono`:

| Old shape | New shape |
| --- | --- |
| `fc::time_point` | `std::chrono::sys_time<std::chrono::microseconds>` |
| `fc::time_point_sec` | `std::chrono::sys_seconds` |
| `fc::microseconds` | `std::chrono::microseconds` |

Raw compatibility:

- `sys_time<microseconds>` packs as old microseconds since epoch;
- `sys_seconds` packs as old `uint32_t` seconds since epoch;
- out-of-range `sys_seconds` is a hard error.

## Variant, JSON And YAML

JSON is no longer a class facade. Use namespace codec functions:

```cpp
import fcl.json;
import fcl.yaml;

auto from_json = fcl::json::read<config>(json_text);
auto from_yaml = fcl::yaml::read<config>(yaml_text);
auto written = fcl::json::write(value);
```

Both codecs map backend parse/type/schema errors into FCL diagnostics. Backend
types from Glaze do not leave FCL public APIs.

## Exceptions

Old exception hierarchy and old throw/declare macros are removed. New errors are
std-compatible:

```cpp
#include <fcl/exceptions/macros.hpp>

import fcl.exceptions;

try {
   load_config();
} FCL_CAPTURE_AND_RETHROW(
   "config load failed",
   fcl::exceptions::ctx("source", "service.yaml"),
   fcl::exceptions::secret("passphrase", passphrase))
```

Catch `std::exception` at process boundaries. Use
`fcl::exceptions::context_error` only when you specifically need structured fields.

## Logging

Preferred new path:

```cpp
import fcl.log.logger;
import fcl.log.record;

auto log = fcl::logger{"service"};
log.add_sink(std::make_shared<fcl::console_sink>());
log.info("started", {fcl::log_ctx("component", "api")});
log.error("failed", {fcl::log_secret("token", token)});
```

Use `fcl::exceptions::set_log_sink(...)` to route exception capture into the logger.
`fcl_log` remains sync-only; async logging should be a downstream adapter if
needed.

## Crypto

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <string>

import fcl.crypto.asymmetric;
import fcl.crypto.sha256;
import fcl.raw.raw;

struct signed_payload {
   std::uint64_t account = 0;
   std::uint64_t sequence = 0;
   std::string action;

   [[nodiscard]] fcl::crypto::bytes signing_bytes(const fcl::crypto::sha256& chain_id) const;
};

BOOST_DESCRIBE_STRUCT(signed_payload, (), (account, sequence, action))

inline fcl::crypto::bytes signed_payload::signing_bytes(const fcl::crypto::sha256& chain_id) const {
   auto bytes = fcl::crypto::bytes{};
   fcl::raw::pack(bytes, chain_id);
   fcl::raw::pack(bytes, *this);
   return bytes;
}

auto private_key = fcl::crypto::asymmetric::private_key::generate();
auto expected_public_key = private_key.get_public_key();

auto chain_id = fcl::crypto::sha256{}; // Replace with the real chain/domain id.
auto payload = signed_payload{
   .account = 42,
   .sequence = 7,
   .action = "commit",
};

auto message = payload.signing_bytes(chain_id);
auto signature = private_key.sign(message);

auto verified = expected_public_key.verify(message, signature);
```

OpenSSL 3.0+ is the backend baseline. FCL does not shell out to `openssl`. AES-GCM
is the preferred modern symmetric API; CBC/CFB remain compatibility surfaces.
The old FC `digest_type::encoder + fc::raw::pack` pattern maps directly to
`fcl::crypto::sha256::encoder + fcl::raw::pack`. For signatures and protocol hashes, use
described DTOs; do not sign JSON text, formatted strings or manually
concatenated fields.

## App And Runtime

Program shells should move to `application_shell`. It owns the common runtime
members, plugin registry, config collection and lifecycle order:

```cpp
import fcl.app.exceptions;
import fcl.app.application;
import fcl.app.events;
import fcl.app.diagnostics;
import fcl.app.signals;
import fcl.app.plugin_context;
import fcl.app.plugin;
import fcl.app.plugin_registry;
import fcl.app.application_shell;
import fcl.app.application_builder;
import fcl.app.runner;
import fcl.app.daemon;
import fcl.asio.blocking;

auto app = service_application{};
auto registry = app.describe_config();
auto document = load_config(registry);

app.configure(document);
fcl::asio::blocking::run(app.runtime(), app.startup());
app.request_stop();
fcl::asio::blocking::run(app.runtime(), app.shutdown());
```

Plugins describe config through `describe_config()` and receive
`config::component_view`; they do not parse CLI/YAML directly.
Use lower-level `application_runtime` only when an existing host framework
already owns runtime, ports, events, signals and diagnostics.

## Review Checklist

- No `<fc/...>` includes.
- No `namespace fc`, `fc::`, `FC_REFLECT` or transitional reflection aliases.
- Raw golden tests prove byte compatibility for every migrated contract shape.
- Secrets use `secret(...)`/`log_secret(...)`.
- Consumers link leaf targets unless they intentionally need `FCL::fcl`.
