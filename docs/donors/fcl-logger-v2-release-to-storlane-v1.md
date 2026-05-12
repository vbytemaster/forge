# Donor Trace: FCL Logger v2 Release-To-Storlane v1

## Scope

This note records donor patterns used for the synchronous FCL logger v2. Donors
are references, not build dependencies, except private Boost.Stacktrace fallback.

## Boost.Stacktrace

Accepted:

- stacktrace capture as an optional diagnostic;
- basic backend as a private fallback when `std::stacktrace` is unavailable.

Rejected:

- exposing `boost::stacktrace` in public FCL modules;
- making stacktrace availability a hard consumer requirement.

## spdlog

Accepted:

- cheap level checks before building expensive records;
- small sink interface;
- text and structured output as separate sink concerns.

Rejected:

- importing spdlog as a dependency;
- pattern-string formatting as the public FCL API.

## Quill

Accepted:

- caller path should be cheap when the log level is disabled;
- structured fields should avoid unnecessary work before level checks.

Rejected:

- async queue in the core logger;
- logger-owned runtime threads.

## Boost.Log Attributes

Accepted:

- context/attributes model: component, logger name, source location, thread and
  structured fields belong on the record.

Rejected:

- broad Boost.Log dependency;
- global mutable attribute registry as the central FCL model.

## FCL Target Shape

The resulting logger is sync-only:

- `log_record` is the central value;
- `sink` owns output;
- `logger` owns level checks and dispatch;
- stacktrace backend is private;
- exception capture is connected by callback, not by dependency from
  `fcl_exception` to `fcl_log`.
