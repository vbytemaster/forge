# fcl_otlp

`fcl_otlp` is the opt-in OpenTelemetry Protocol adapter for FCL logs. It
exports `fcl_log` records to an OTLP/HTTP JSON collector and can resend
minimal crash-spool records on the next start.

## Responsibility

- `fcl_log` owns log records, structured fields, redaction and synchronous sink
  mechanics.
- `fcl_http` owns HTTP client mechanics.
- `fcl_asio` owns runtime scheduling, timers and cancellation.
- `fcl_otlp` owns OTLP log JSON mapping, bounded export queues, retry policy,
  explicit flush/shutdown, and crash-spool resend.

Telemetry export is opt-in. Installing `fcl_log` does not automatically create
an exporter or contact a collector.

## Public Modules

- `fcl.otlp.log_exporter` - OTLP/HTTP JSON log exporter and export metrics.
- `fcl.otlp.log_sink` - non-blocking `fcl::sink` adapter with bounded queue
  semantics.
- `fcl.otlp.options` - resource, queue, batch and retry options.
- `fcl.otlp.crash` - POSIX crash-spool install and next-start resend helpers.
- `fcl.otlp.exceptions` - typed OTLP exception category.
- `fcl.otlp` - aggregate module.

Target: `fcl_otlp`.

Dependencies: `fcl_exceptions`, `fcl_log`, `fcl_asio` and `fcl_http`. The
library does not depend on `opentelemetry-cpp`, gRPC, protobuf, backend SDKs,
P2P, plugins or product code.

## Examples

### Export Logs

```cpp
import fcl.asio.runtime;
import fcl.log.logger;
import fcl.otlp;

auto runtime = fcl::asio::runtime{};
auto exporter = std::make_shared<fcl::otlp::log_exporter>(
   runtime,
   fcl::otlp::log_exporter_options{
      .endpoint = "http://127.0.0.1:4318",
      .logs_path = "/v1/logs",
      .resource = {.attributes = {{"service.name", "fcl-app"}}},
   });

auto log = fcl::logger{"app"};
log.add_sink(std::make_shared<fcl::otlp::log_sink>(exporter));
log.info("startup complete");

co_await exporter->async_flush();
```

### Resend Crash Spool

```cpp
import fcl.otlp;

auto guard = fcl::otlp::install_crash_capture(
   fcl::otlp::crash_spool_options{.directory = "./crash-spool"});

auto result = co_await fcl::otlp::async_resend_crashes(
   *exporter,
   fcl::otlp::crash_spool_options{.directory = "./crash-spool"});
```

Crash capture writes only bounded, fixed-size records from signal-safe paths.
Human-readable JSON, HTTP export, symbolication and redaction run later during
normal process startup. By default `chain_after_capture` remains enabled, so
FCL records evidence and then returns to the platform/default crash handling;
tests or supervised helpers may disable it to exit cleanly after capture.

## Tests

- `test_fcl_otlp` covers OTLP JSON shape, batching, queue overflow, retry,
  shutdown, redaction and crash-spool resend.
- `test_fcl_package_otlp_component` verifies
  `find_package(FCL CONFIG REQUIRED COMPONENTS otlp)` and `FCL::fcl_otlp`.
- `test_fcl_log`, `test_fcl_http_websocket` and `test_fcl_app` cover adjacent
  logging, HTTP and app behavior.

## Boundaries

- Logs and minimal crash evidence are in scope.
- Metrics and traces are future exporters with separate runtime semantics.
- Native minidumps, Crashpad, Breakpad, watchdogs, gRPC and protobuf are out of
  scope for this library version.
- Product authorization, privacy policy, alert routing and storage retention
  belong above FCL.
