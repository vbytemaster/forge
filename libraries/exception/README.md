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
   fcl::error::ctx("path", "/tmp/fcl.vault"),
   fcl::error::secret("passphrase", "not logged"));
```

`secret(...)` values render as `<redacted>` in `what()`,
`format_exception_chain()` and log helpers.

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

### Deadline Check

```cpp
#include <fcl/exception/macros.hpp>

FCL_CHECK_DEADLINE(deadline, fcl::error::ctx("phase", "handshake"));
```

This throws a std-compatible `context_error` with `std::errc::timed_out`.

## Typical Mistakes

- Do not catch only `fcl::error::context_error` at process boundaries. Also catch
  `std::exception` because many FCL layers intentionally throw standard errors.
- Do not put secrets into the plain message string. Use `secret(key, value)`.
- Do not use `FCL_CAPTURE_AND_LOG` on correctness paths if the error must
  propagate; logging must not become silent recovery.

## Tests

`test_fcl_exception` covers redaction, nested exception chains, assertion macro
behavior and deadline errors.
