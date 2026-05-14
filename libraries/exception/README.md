# fcl_exception

`fcl_exception` is a std-based context layer for errors: it adds structured,
redacted diagnostic context to ordinary C++ exceptions without bringing back the
old FC exception hierarchy.

## When To Use

- Wrap a lower-level exception with phase/resource context.
- Throw assertion and deadline failures with source location and typed context.
- Format nested exception chains for diagnostics without depending on `fcl_log`.

## When Not To Use

- Do not model schema/config validation errors here; those live in `fcl_schema`.
- Do not serialize exceptions through `variant` or JSON.
- Do not use it as a business error taxonomy. Product/application code owns its
  own typed errors.

## Public API

Module:

- `fcl.exception.exception`

Macro-only header:

- `fcl/exception/macros.hpp`

Target: `fcl_exception`.

Dependencies: `fcl_core` only. It must not import `log`, `variant`, `json`,
`raw` or `crypto`.

## Examples

### Throw With Context

```cpp
#include <fcl/exception/macros.hpp>

import fcl.exception.exception;

FCL_THROW(
   "cannot open vault",
   fcl::error::ctx("path", "fcl.vault"),
   fcl::error::secret("passphrase", "not logged"));
```

`secret(...)` values render as `<redacted>` in `what()`,
`format_exception_chain()` and log helpers.

### Assert With Debug Context

```cpp
#include <fcl/exception/macros.hpp>

import fcl.exception.exception;

void open_slot(std::uint32_t index, std::uint32_t capacity) {
   FCL_ASSERT(
      index < capacity,
      "slot index is out of range",
      fcl::error::ctx("index", index),
      fcl::error::ctx("capacity", capacity));
}
```

`FCL_ASSERT` throws a std-compatible `context_error`; callers should still catch
`std::exception` at process boundaries because other FCL layers intentionally use
standard exceptions such as `std::invalid_argument` and `std::out_of_range`.

### Preserve Nested Cause

```cpp
#include <fcl/exception/macros.hpp>

import fcl.exception.exception;

try {
   parse_config();
} FCL_CAPTURE_AND_RETHROW(
   "config bootstrap failed",
   fcl::error::ctx("component", "http"))
```

The rethrow uses `std::throw_with_nested`, so callers can inspect the outer
`fcl::error::context_error` and the original inner exception.

### Format A Nested Exception Chain

```cpp
#include <fcl/exception/macros.hpp>

import fcl.exception.exception;

try {
   try {
      parse_config();
   } FCL_CAPTURE_AND_RETHROW(
      "config load failed",
      fcl::error::ctx("source", "service.yaml"),
      fcl::error::secret("passphrase", passphrase))
} catch (const std::exception& error) {
   auto chain = fcl::error::format_exception_chain(error);
   // chain contains outer context, inner std::exception::what(), and redacted secrets.
}
```

### Deadline Check

```cpp
#include <fcl/exception/macros.hpp>

FCL_CHECK_DEADLINE(deadline, fcl::error::ctx("phase", "handshake"));
```

This throws a std-compatible `context_error` with `std::errc::timed_out`.

### Process Boundary With Graceful Shutdown

At a daemon boundary, catch `std::exception`, format the chain, request
shutdown, and return an error. Do not turn recoverable startup failures into
`abort()` or detached cleanup.

```cpp
#include <fcl/exception/macros.hpp>

import fcl.exception.exception;

int run_service() {
   try {
      return run_foreground();
   } catch (const std::exception& error) {
      auto chain = fcl::error::format_exception_chain(error);
      report_startup_failure(chain);
      request_stop_noexcept();
      shutdown_best_effort();
      return 1;
   }
}
```

The chain is diagnostic text, not a control-flow taxonomy. Product code should
use typed domain errors for decisions such as retry, backoff or user messaging.

### Route Capture Logs To `fcl_log`

`fcl_exception` exposes a neutral callback. The consuming program may route that
callback to `fcl_log`, syslog, a test capture vector or any other sink.

```cpp
#include <fcl/exception/macros.hpp>

import fcl.exception.exception;
import fcl.log.logger;
import fcl.log.record;

auto log = fcl::logger{"worker"};

fcl::error::set_log_sink([&](std::string_view chain) {
   log.error(
      "cleanup failed",
      {
         fcl::log_ctx("exception-chain", chain),
         fcl::log_secret("session-token", token),
      });
});

try {
   cleanup_best_effort();
} FCL_CAPTURE_AND_LOG(
   "cleanup best-effort path failed",
   fcl::error::ctx("phase", "shutdown"),
   fcl::error::secret("session-token", token))
```

`FCL_CAPTURE_AND_LOG` deliberately swallows the current exception after routing
it to the callback. Use it only for cleanup paths where continuing is correct.
For correctness paths, use `FCL_CAPTURE_AND_RETHROW` or
`FCL_CAPTURE_LOG_AND_RETHROW`.

## Risks And Anti-Patterns

- Do not convert every error into `context_error`. Use standard exception types
  when no structured context is needed.
- Do not log-and-continue from correctness paths. Capture helpers must preserve
  failure semantics, not create silent recovery.
- Do not expose secret values through messages, `what()` strings or field names.
  Use `secret(key, value)` for data that may be sensitive.

## Typical Mistakes

- Do not catch only `fcl::error::context_error` at process boundaries. Also catch
  `std::exception` because many FCL layers intentionally throw standard errors.
- Do not put secrets into the plain message string. Use `secret(key, value)`.
- Do not use `FCL_CAPTURE_AND_LOG` on correctness paths if the error must
  propagate; logging must not become silent recovery.
- Do not call `std::terminate()`/`abort()` just to get a stack trace. Capture
  context, log the chain, and let the application lifecycle shut down.
- Do not branch on substrings from `what()`. Context fields are for diagnostics;
  product control flow should use typed errors or explicit result codes.

## Tests

`test_fcl_exception` covers redaction, nested exception chains, assertion macro
behavior and deadline errors.
