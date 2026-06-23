# forge::plugins::log::otlp

`forge::plugins::log::otlp` connects existing `forge_log` loggers to the
`forge_otlp` HTTP/JSON log exporter. It does not add a new logging API: code
continues to use `ilog`, `wlog`, `elog`, `dlog` for the default logger and
`forge_ilog(logger, ...)` style macros for named logger routes.

## Package

```cmake
find_package(Forge REQUIRED COMPONENTS plugins_log_otlp)
target_link_libraries(app PRIVATE Forge::forge_plugins_log_otlp)
```

```cpp
import forge.plugins.log.otlp.plugin;

registry.register_plugin(forge::plugins::log::otlp::descriptor());
```

Runtime identity:

- Plugin id: `forge.plugins.log.otlp`
- Main API id: `forge.plugins.log.otlp`
- Config section: `plugins.log.otlp`
- Target/component: `forge_plugins_log_otlp` / `plugins_log_otlp`

## Configuration

Configuration is schema-driven through `BOOST_DESCRIBE_STRUCT`,
`forge::schema::rules<T>` and `forge::config::decode<T>()`.

```yaml
plugins:
  log:
    otlp:
      enabled: true
      endpoint: "http://localhost:4318"
      logs-path: "/v1/logs"
      protocol: "http-json"

      loggers:
        - name: "default"
          enabled: true
          level: "info"
          export: true
        - name: "network"
          enabled: true
          level: "debug"
          export: true

      resource:
        attributes:
          - key: "service.name"
            value: "forge-node"

      queue:
        max-records: 8192
        max-bytes: 8388608
        overflow: "drop-new"

      batch:
        max-records: 512
        max-bytes: 524288
        flush-interval-ms: 5000

      retry:
        max-attempts: 3
        base-delay-ms: 100
        max-delay-ms: 5000

      request-timeout-ms: 30000
      shutdown-timeout-ms: 5000

      crash-spool:
        enabled: false
        directory: "./crash-spool"
        resend-on-startup: true
```

`enabled: false` is a no-op: no exporter is created, no sink is attached and no
network work is started.

## Logging

Default logger:

```cpp
ilog("node started");
```

Named logger:

```cpp
static auto network_log = forge::logger::get("network");

forge_ilog(network_log, "peer connected ${peer}", ("peer", peer_id));
```

The plugin attaches one shared OTLP sink to every configured `loggers[]` route
with `export: true`. Logger names are user-defined; the plugin does not hardcode
product domains.

## Management API

The plugin exposes a local-only management API:

```cpp
auto logs = context.apis().get<forge::plugins::log::otlp::api>(
   {.id = {"forge.plugins.log.otlp"}, .major = 1});

co_await logs->flush();
auto metrics = co_await logs->metrics();
```

`flush()` waits for queued OTLP log records to be exported. `metrics()` returns
queue/export counters from the underlying `forge_otlp` exporter.

## Boundaries

- Logs only in this plugin; metrics and traces are future separate plugins.
- `forge_log` owns logger names, macros and sink dispatch.
- `forge_otlp` owns OTLP JSON mapping, batching, retry and crash-spool mechanics.
- The plugin owns only config/lifecycle wiring.
- No OpenTelemetry SDK globals, environment auto-discovery, new logging macros,
  direct `send(record)` API, auth policy or product logger names are introduced.
