# Migration Guide: storlane-fc To FCL

## Status

FCL is a breaking source API. It does not restore `fc::...` source
compatibility. Compatibility is guaranteed only where tests preserve old raw
wire bytes, primarily through `fcl::raw::pack/unpack` for retained types.

## CMake

Prefer leaf targets:

```cmake
find_package(FCL CONFIG REQUIRED)

target_link_libraries(my_program PRIVATE
   FCL::fcl_raw
   FCL::fcl_crypto
   FCL::fcl_app
   FCL::fcl_log
)
```

Use `FCL::fcl` only when the consumer intentionally wants every FCL feature and
its transitive dependencies.

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
#include <fcl/exception/macros.hpp>
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
import fcl.raw.raw;

auto bytes = std::vector<char>{};
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
#include <fcl/exception/macros.hpp>

import fcl.exception.exception;

try {
   load_config();
} FCL_CAPTURE_AND_RETHROW(
   "config load failed",
   fcl::error::ctx("source", "service.yaml"),
   fcl::error::secret("passphrase", passphrase))
```

Catch `std::exception` at process boundaries. Use
`fcl::error::context_error` only when you specifically need structured fields.

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

Use `fcl::error::set_log_sink(...)` to route exception capture into the logger.
`fcl_log` remains sync-only; async logging should be a downstream adapter if
needed.

## Crypto

```cpp
import fcl.crypto.sha256;
import fcl.crypto.aes;
import fcl.crypto.random;
```

OpenSSL 3 is the backend baseline. FCL does not shell out to `openssl`. AES-GCM
is the preferred modern symmetric API; CBC/CFB remain compatibility surfaces.

## App And Runtime

Program shells should move to `application_base` plus `application_runtime`:

```cpp
import fcl.app;
import fcl.asio.blocking;

auto app = service_app{make_plugins()};
fcl::asio::blocking::run(app.runtime(), app.initialize());
fcl::asio::blocking::run(app.runtime(), app.startup());
app.request_stop();
fcl::asio::blocking::run(app.runtime(), app.shutdown());
```

Plugins describe config through `describe_config()` and receive
`config::component_view`; they do not parse CLI/YAML directly.

## Review Checklist

- No `<fc/...>` includes.
- No `namespace fc`, `fc::`, `FC_REFLECT` or transitional reflection aliases.
- Raw golden tests prove byte compatibility for every migrated contract shape.
- Secrets use `secret(...)`/`log_secret(...)`.
- Consumers link leaf targets unless they intentionally need `FCL::fcl`.
