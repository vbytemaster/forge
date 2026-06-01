# FCL Std-Based Exception Context v1

## Summary

This pass replaces the old FC-style exception hierarchy with a small `std`-based error context layer.

The target name stays `fcl_exceptions` for this iteration, but the public semantics are now `fcl::exceptions`: structured context, redaction, assertions, deadline checks and nested exception-chain formatting.

## Goals

- Make FCL exceptions interoperable with normal `std::exception` handling.
- Preserve the useful capture ergonomics of FC-style macros without keeping the old hierarchy.
- Keep exception support independent from logging, variant, JSON, raw serialization and crypto.
- Make secret fields render as `<redacted>` in messages, logs and formatted exception chains.

## Public API

- `fcl::exceptions::field` stores a key/value pair and a redaction bit.
- `fcl::exceptions::ctx(key, value)` creates a safe diagnostic field.
- `fcl::exceptions::secret(key, value)` creates a redacted diagnostic field.
- `fcl::exceptions::context_error` inherits from `std::runtime_error` and stores:
  - message;
  - structured fields;
  - `std::source_location`;
  - optional `std::error_code`.
- `fcl::exceptions::format_exception_chain(...)` formats nested `std` exceptions.
- `fcl::exceptions::log_current_exception(...)` logs the current exception chain through a neutral sink or stderr fallback.

Macro-only helpers live in `include/fcl/exceptions/macros.hpp` because C++ modules cannot export macros:

- `FCL_THROW(message, ...)`;
- `FCL_ASSERT(test, ...)`;
- `FCL_CHECK_DEADLINE(deadline, ...)`;
- `FCL_CAPTURE_AND_RETHROW(message, ...)`;
- `FCL_CAPTURE_LOG_AND_RETHROW(message, ...)`;
- `FCL_CAPTURE_AND_LOG(message, ...)`.

All structured macro fields must be explicit `fcl::exceptions::ctx(...)` or `fcl::exceptions::secret(...)` calls. The old tuple-like capture syntax is not valid FCL API.

## Removed Surfaces

The old FC-owned hierarchy, declare/throw/rethrow macro family, dynamic exception copying, special wrapper types, variant-backed exception serialization and raw exception packing are removed.

FCL no longer treats exceptions as serializable data. Reports that need stable machine-readable output should define their own DTOs and serialize those through the relevant JSON/config/report layer.

## Error Mapping

- JSON parse and conversion failures use `std::invalid_argument` or `fcl::exceptions::context_error`.
- Raw datastream bounds failures use `std::out_of_range`.
- Crypto invalid input and assertion failures use `std::invalid_argument`, `std::logic_error` or `fcl::exceptions::context_error`.
- Deadline checks use `fcl::exceptions::context_error` with `std::errc::timed_out`.

## Dependency Boundary

`fcl_exceptions` may depend on `fcl_core` and standard/Boost headers only.

It must not import or link:

- `fcl_log`;
- `fcl_variant`;
- `fcl_json`;
- `fcl_raw`;
- `fcl_crypto`.

Logging integration is intentionally one-way: callers may route formatted exception chains into a logger, but the exception layer does not own the logging backend.

## Redaction Rules

Use `secret(...)` for passphrases, private keys, tokens, raw key material, recovery material and any other sensitive value.

Secret fields must render as `<redacted>` everywhere:

- `context_error::what()`;
- `format_fields`;
- `format_exception_chain`;
- `log_current_exception`.

## Tests

`test_fcl_exceptions` covers:

- normal and redacted field formatting;
- source location and optional `std::error_code`;
- nested exception preservation;
- formatted exception chain output;
- assertion and deadline macros.

Existing JSON, raw and crypto tests were updated to expect `std`-compatible errors instead of the removed FC-style exception hierarchy.
