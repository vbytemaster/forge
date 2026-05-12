# fcl_log

`fcl_log` is the retained lightweight logging layer: log levels, contexts,
messages, logger hierarchy and appenders. It is intentionally not the exception
system and not a structured audit pipeline.

## When To Use

- A library needs a small logger/appender boundary.
- Log messages need source location, thread name and variant-backed key/value
  data.
- Existing FC-style logging macros need a maintained FCL home.

## When Not To Use

- Do not use logs as a security/audit source of truth.
- Do not log secrets and rely on appenders to save you. Redact before logging.
- Do not call logging directly from latency-sensitive hot paths unless the
  consuming product explicitly enables that trace level.

## Public Modules

- `fcl.log.log_message` — `log_level`, `log_context`, `log_message`.
- `fcl.log.logger` — logger hierarchy and appender management.
- `fcl.log.appender`, `fcl.log.console_appender`, `fcl.log.logger_config`.

Macro-only header:

- `fcl/log/macros.hpp`

Target: `fcl_log`.

Dependencies: `fcl_core`, `fcl_reflect`, `fcl_variant`, Boost headers and
private Boost.DLL.

## Examples

### Create A Message

```cpp
#include <fcl/log/macros.hpp>

import fcl.log.logger;

auto log = fcl::logger::get("network");
fcl_ilog(log, "connected peer ${peer}", ("peer", peer_id));
```

### Attach A Console Appender

```cpp
import fcl.log.console_appender;
import fcl.log.logger;

auto log = fcl::logger::get("app");
log.add_appender(std::make_shared<fcl::console_appender>());
log.set_log_level(fcl::log_level::info);
```

### Preserve Context

```cpp
#include <fcl/log/macros.hpp>

auto message = FCL_LOG_MESSAGE(warn, "retry ${attempt}", ("attempt", 3));
auto text = message.get_limited_message();
```

## Security Notes

The logger does not know which arbitrary fields are secret. Callers must redact
tokens, private keys, passphrases and storage handles before creating
`log_message` data.

## Typical Mistakes

- Do not use old `fc_*` logging macros.
- Do not put product trace schemas in `fcl_log`; products should own their trace
  schema and may use FCL logger as a sink.
- Do not convert exceptions into logs and then continue unless the path is an
  explicit cleanup/best-effort path.

## Tests

Logging is exercised through retained log message/context tests and downstream
exception/diagnostic flows. New appenders should add focused tests around
formatting, redaction expectations and failure isolation.
