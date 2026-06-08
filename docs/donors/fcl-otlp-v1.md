# FCL OTLP v1 Donor Traceability

This is a traceability note, not a roadmap. The canonical block order stays in
`docs/network/quic-p2p.md`.

## Goal

`fcl_otlp` provides the first reusable FCL observability export path: structured
`fcl_log` records are exported to an operator-configured OpenTelemetry Collector
through OTLP/HTTP JSON. It also owns optional crash-spool resend for fatal
process evidence captured before the next start. The exporter is optional,
bounded and transport-owned; `fcl_log` remains synchronous and does not import
runtime or HTTP dependencies.

## Donor Sources

| Donor | Sources inspected | Accepted patterns |
| --- | --- | --- |
| OpenTelemetry OTLP protocol | `https://opentelemetry.io/docs/specs/otlp/` | Logs use OTLP/HTTP endpoint `/v1/logs`, JSON request body and `Content-Type: application/json`; retryable statuses include `429`, `502`, `503` and `504`; `Retry-After` is authoritative when present. |
| OpenTelemetry exporter config | `https://opentelemetry.io/docs/specs/otel/protocol/exporter/` | HTTP/JSON is a supported exporter protocol and endpoint selection is operator configuration. |
| FCL logging rules | `AGENTS.md`, `libraries/log` | Logging core is synchronous and small; async/network export belongs in an adapter, not in `fcl_log`. |
| FCL HTTP/runtime layers | `libraries/http`, `libraries/asio` | Reuse existing HTTP client and Asio runtime instead of introducing a second networking stack. |
| POSIX signal-safety rules | `signal-safety(7)`, platform `sigaction` docs | Crash signal handler writes a fixed binary record with async-signal-safe operations only; no allocation, logging, JSON, HTTP or locks in signal context. |
| C++ terminate handling | `std::set_terminate`, FCL typed exceptions | Terminate path may inspect the current typed exception category/code and capture stacktrace addresses, but it must not persist raw exception text or application context. |
| Native minidump crash reporters | public design docs for minidump-oriented crash capture | Accepted design lesson: separate crash-time capture from upload/symbolication. Rejected dependency: no backend SDK or minidump runtime in this FCL block. |

## FCL Decisions

- `fcl_otlp` owns OTLP options, resource attributes, bounded queueing, batching,
  retry/backoff, HTTP export and shutdown flushing.
- `fcl::otlp::log_sink` implements `fcl::sink` and never performs HTTP work
  inline on the logger call path.
- Queue overflow drops the newest log record and increments exporter metrics.
  The exporter never creates hidden unbounded queues.
- OTLP payload uses lowerCamelCase JSON field names and decimal strings for
  64-bit timestamp values.
- Redacted `fcl_log` fields remain redacted in outgoing OTLP payloads.
- Retry is limited to retryable OTLP/HTTP statuses and connection failures.
  Permanent failures are accounted as failed and dropped records.
- `fcl::otlp::crash_guard` installs at most one active crash capture owner and
  restores signal/terminate handlers on scope exit.
- Crash signal capture writes private fixed-size binary records into a
  per-process spool file. The handler performs no allocation, logger calls,
  JSON encoding, HTTP work, symbolication or redaction.
- Production defaults chain to platform/default crash handling after capture.
  Tests can disable chaining so helper processes exit cleanly after writing
  evidence without making CTest treat the child crash as the test failure.
- Next-start resend validates checksums, quarantines malformed/truncated spool
  files as `.bad`, exports safe fatal evidence through `log_exporter` and
  removes spool files only after successful export.
- Crash resend records typed exception category/code and stacktrace addresses
  where available, but not raw `what()` messages or application context.
- Metrics export and trace/span export are future additive blocks.
- FCL does not require backend-specific observability SDKs or a gRPC transport
  for this first logs exporter.

## Supported Behaviors And Tests

| Behavior | Source | FCL coverage |
| --- | --- | --- |
| Logs POST to `/v1/logs` with JSON content type | OTLP protocol | `test_fcl_otlp log_sink_exports_otlp_json_to_logs_endpoint` |
| Resource attributes and log fields map into OTLP JSON | OTLP logs data model | `test_fcl_otlp log_sink_exports_otlp_json_to_logs_endpoint` |
| Redacted fields never expose secret values | FCL logging rules | `test_fcl_otlp log_sink_exports_otlp_json_to_logs_endpoint` |
| Batch flush by count and explicit flush | OTLP exporter behavior, FCL runtime rules | `test_fcl_otlp exporter_batches_by_count_and_explicit_flush` |
| Bounded queue drops without blocking logger | FCL runtime rules | `test_fcl_otlp bounded_queue_drops_newest_without_blocking_logger` |
| Retryable HTTP status and `Retry-After` handling | OTLP protocol | `test_fcl_otlp exporter_retries_retryable_status_and_drops_permanent_failure` |
| Permanent HTTP failure is not retried | OTLP protocol | `test_fcl_otlp exporter_retries_retryable_status_and_drops_permanent_failure` |
| Shutdown completes within configured deadline | FCL deterministic shutdown rule | `test_fcl_otlp shutdown_flushes_or_drops_within_deadline` |
| Crash capture options and single active guard are typed | FCL ownership and typed exception rules | `test_fcl_otlp crash_spool_options_and_single_active_guard_are_typed` |
| Terminate spool resends safe fatal evidence only | C++ terminate handling, FCL redaction boundary | `test_fcl_otlp next_start_resends_terminate_spool_as_safe_fatal_log` |
| Signal spool resends minimal signal evidence | POSIX signal-safety rules | `test_fcl_otlp next_start_resends_signal_spool` |
| Permanent export failure leaves spool for retry | OTLP permanent failure behavior, crash durability boundary | `test_fcl_otlp permanent_export_failure_leaves_spool_for_retry` |
| Malformed spool is quarantined and resend is bounded | FCL deterministic recovery and bounded memory rules | `test_fcl_otlp malformed_spool_is_quarantined_and_resend_is_bounded` |

## Non-Goals

- No metrics registry export and no trace/span export in G.6a/G.6b.
- No minidump backend, symbolication service or backend-specific crash SDK in
  G.6b.
- No backend-specific SDK integration and no direct Collector replacement.
