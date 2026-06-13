# fcl_log

`fcl_log` — синхронный C++23 logging core для библиотек и программ, которым
нужны дешёвая проверка уровня, structured fields, source location, thread
identity, JSONL/text sinks, redaction и диагностический stacktrace без
зависимости на runtime/event loop.

Библиотека сохраняет старый `log_message`/appender слой как compatibility
поверхность, но новый код должен использовать `log_record`, `log_field`,
`logger::info/error(...)` и sinks. Логгер не является audit/security boundary:
секреты нужно помечать как secret до записи.

## When To Use

- Нужны синхронные console/file/JSONL logs без отдельного logging daemon.
- Нужно записывать structured context: component, fields, exception chain,
  source location, timestamp, thread id/name.
- Нужно автоматически добавлять stacktrace на error/fatal-like путях, но не
  платить за него на `debug`/`info`.
- Нужно направить `fcl_exceptions` capture path в logging sink без зависимости
  `fcl_exceptions -> fcl_log`.

## When Not To Use

- Не используйте `fcl_log` как durable audit trail или source of truth.
- Не добавляйте сюда async queue/background runtime: это будущий runtime adapter,
  а не обязанность core logger.
- Не пишите secrets обычными полями. Используйте `log_secret(...)`.
- Не держите application-specific trace schema в FCL. Приложение может использовать
  `fcl_log` как sink, но schema принадлежит продукту.

## Public Modules

- `fcl.log.record` — `log_record`, `log_field`, sinks, stacktrace snapshot.
- `fcl.log.logger` — logger hierarchy, level checks, v2 logging API.
- `fcl.log.log_message` — retained message formatter.
- `fcl.log.appender`, `fcl.log.console_appender`, `fcl.log.logger_config` —
  retained appender compatibility.

Macro-only header:

- `fcl/log/macros.hpp` — retained convenience macros and modern `fcl_log(...)`.

Target: `fcl_log`.

Dependencies: `fcl_core`, `fcl_reflect`, `fcl_variant`, Boost headers,
private Boost.DLL and optional private Boost.Stacktrace fallback. Public API
does not expose `std::stacktrace` or `boost::stacktrace`.

## Stacktrace Backend

Backend order:

1. use `std::stacktrace` when the toolchain exposes `<stacktrace>` and the
   feature macro;
2. otherwise use private `Boost::stacktrace_basic` when available;
3. otherwise return `stacktrace_unavailable`.

Consumers always see only `fcl::stacktrace_snapshot`. Missing stacktrace support
is a degraded diagnostic mode, not a build failure for consumers that do not
need stack traces.

## Examples

### Attach Sinks

```cpp
#include <memory>

import fcl.log.logger;
import fcl.log.record;

auto log = fcl::logger{"service"};
log.set_log_level(fcl::log_level::debug);
log.add_sink(std::make_shared<fcl::console_sink>());
log.add_sink(std::make_shared<fcl::jsonl_sink>("service.jsonl"));
```

### Write Structured Logs

```cpp
import fcl.log.logger;
import fcl.log.record;

log.info(
   "listener started",
   {
      fcl::log_ctx("component", "http"),
      fcl::log_ctx("bind", "127.0.0.1:8080"),
   });

log.error(
   "login failed",
   {
      fcl::log_ctx("user", "alice"),
      fcl::log_secret("access-token", token),
   });
```

`log_secret(...)` stores `<redacted>` in text and JSONL output. Do not put
tokens, private keys or passphrases into the plain message string.

### Avoid Building Disabled Records

Use the `fcl_log(...)` macro when a field is expensive to compute. The provider
is evaluated only after `logger.is_enabled(level)`.

```cpp
#include <fcl/log/macros.hpp>

import fcl.log.logger;
import fcl.log.record;

fcl_log(
   log,
   fcl::log_level::debug,
   "scheduler snapshot",
   fcl::log_field_provider{[&] {
      return fcl::log_ctx("queue-depth", expensive_queue_depth());
   }});
```

For cheap fields, direct `logger.info(...)`/`logger.error(...)` is clearer.

### Route Exception Capture Into Logger

`fcl_exceptions` owns the capture helpers, but it does not depend on `fcl_log`.
A program wires them together explicitly at the edge.

```cpp
#include <fcl/exceptions/macros.hpp>

import fcl.exceptions;
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
import fcl.log.logger;
import fcl.log.record;

fcl::exceptions::set_log_sink([&](std::string_view chain) {
   log.error(
      "operation failed",
      {
         fcl::log_ctx("exception-chain", chain),
         fcl::log_secret("request-token", token),
      });
});

try {
   run_operation();
} FCL_CAPTURE_AND_LOG(
   "operation failed",
   fcl::exceptions::ctx("phase", "startup"),
   fcl::exceptions::secret("request-token", token))
```

Use `FCL_CAPTURE_AND_LOG` only for explicit cleanup/best-effort paths. If the
operation must fail the caller, use `FCL_CAPTURE_AND_RETHROW` or
`FCL_CAPTURE_LOG_AND_RETHROW`.

### Log Runtime Failures Without Turning Logs Into Recovery

```cpp
#include <fcl/exceptions/macros.hpp>

import fcl.exceptions;
import fcl.log.logger;
import fcl.log.record;

boost::asio::awaitable<void> start_with_logging(fcl::app::application_shell& app) {
   try {
      co_await app.startup();
   } catch (const std::exception& error) {
      log.error(
         "startup failed",
         {
            fcl::log_ctx("exception-chain", fcl::exceptions::format_exception_chain(error)),
            fcl::log_secret("bootstrap-token", token),
         });
      app.request_stop();
      co_await app.shutdown();
      throw;
   }
}
```

The log call records context; it does not make the application healthy. Application
code still owns rollback, shutdown and the returned exit status.

### Format A Record Without A Sink

```cpp
import fcl.log.record;

auto record = fcl::log_record{
   .level = fcl::log_level::warn,
   .logger = "probe",
   .component = "readiness",
   .message = "endpoint slow",
   .fields = {fcl::log_ctx("latency-ms", 250)},
};

auto line = fcl::format_text_log_record(record);
auto json = fcl::format_json_log_record(record);
```

This is useful for tests and adapters that need deterministic formatting.

## Security Notes

- Redaction is explicit: `log_secret(...)` is safe; plain `log_ctx(...)` is not.
- JSONL output is a diagnostic stream, not signed audit data.
- Error logs may include stack traces. Avoid adding raw user payloads or secrets
  to error messages.
- Sinks are synchronous. If a file sink points to slow storage, the caller pays
  that cost.

## Runtime Risks And Anti-Patterns

- Do not log raw serialized payloads or private keys to “debug signatures”.
  Log safe IDs, hashes or redacted config paths instead.
- Do not allocate expensive fields before checking the log level. Use
  `fcl_log(...)` with `log_field_provider` for expensive diagnostics.
- Do not install a slow network filesystem path as a synchronous file sink on a
  hot request path. Route hot-path telemetry through an application-owned trace layer
  or a bounded adapter.
- Do not hide errors by logging and continuing unless the code path is explicitly
  best-effort cleanup.

## Typical Mistakes

- Calling `format_stacktrace()` on hot debug paths.
- Creating `log_field_provider` and then calling `make_log_fields(...)`
  directly before checking the log level.
- Treating exception logging as recovery.
- Reintroducing old lower-level logging macros in new examples. Prefer
  `logger.info(...)`, `logger.error(...)` or `fcl_log(...)`.

## Tests

`test_fcl_log` covers cheap level filtering, console/file/JSONL-style
formatting, secret redaction, stacktrace fallback, and exception-chain routing.
