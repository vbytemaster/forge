# Runtime + App

Этот документ описывает связку `fcl_asio` and `fcl_app`. Локальные API
показаны в [libraries/asio/README.md](../../libraries/asio/README.md) and
[libraries/app/README.md](../../libraries/app/README.md); здесь фиксируется
сквозная архитектура runtime, scheduler and async application lifecycle.

## Задача

Сервисные программы обычно быстро скатываются в набор detached threads,
`std::async`, sleep/poll loops and глобальных очередей. Это плохо проверяется:
непонятно, кто владеет shutdown, где backpressure, кто отменяет delayed work и
почему startup failure оставил часть системы работающей.

FCL baseline другой: runtime is explicit, async API is coroutine-first, queues
are bounded, shutdown is deterministic and diagnostics are part of the contract.

## Layering

```text
program shell
  -> fcl_program_options / fcl_yaml / fcl_json
  -> fcl_config::document
  -> fcl_app::application_shell
  -> plugins configured through component_view
  -> fcl_asio runtime / task_scheduler for async work
```

`fcl_app` consumes config documents and component views. It does not parse
files, environment or command lines. That split keeps application core testable:
tests can feed a `config::document` directly without building argv/YAML fixtures.
`application_runtime` remains a lower-level escape hatch, while
`application_shell` is the preferred production owner of runtime, scheduler,
ports, events, signals, diagnostics, plugin registry and plugin context.

## Runtime Ownership

- `fcl_asio::runtime` owns `boost::asio::io_context` and worker threads.
- `fcl_asio::task_scheduler` owns bounded pending queues, numeric priority,
  delayed tasks, cancellation handles and metrics.
- Priority names are product/program policy. FCL only orders numeric priority and
  FIFO within equal priority.
- Blocking work must cross an explicit boundary; it must not be hidden inside
  unrelated coroutine code.

## Application Lifecycle

The lifecycle order is fixed:

1. app and plugin config descriptors are collected;
2. schema defaults are merged with the input config document;
3. app hook receives `configure_context`;
4. plugins receive `configure(component_view)`;
5. app hook installs ports;
6. plugins initialize in dependency order;
7. plugins start;
8. `request_stop()` is issued synchronously;
9. plugins shut down in reverse order.

All heavy lifecycle methods return `boost::asio::awaitable<void>`. `request_stop`
is intentionally synchronous/noexcept: it signals intent, it does not perform
cleanup.

## Integration Example

```cpp
class service_application final : public fcl::app::application_shell {
 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(make_http_plugin_descriptor());
   }

   void on_describe_config(fcl::config::component_registry& registry) const override {
      registry.add(fcl::config::describe_component<service_config>("service"));
   }
};

auto app = service_application{};
auto registry = app.describe_config();
auto cli = fcl::program_options::parse(argc, argv, registry);

if (!cli.ok()) {
   report_diagnostics(cli.diagnostics);
}

boost::asio::awaitable<void> run_service(service_application& app, const fcl::config::document& document) {
   app.configure(document);
   co_await app.startup();
   app.request_stop();
   co_await app.shutdown();
}
```

The program shell owns CLI/YAML/JSON adapters. `application_shell` sees only an
already-merged `config::document` and controls plugin lifecycle ordering.

For smaller applications and tests, `application_builder` creates an
`application_shell` without introducing another lifecycle model:

```cpp
auto builder = fcl::app::application_builder{};
builder.name("service")
   .config<service_config>("service", [&](const service_config& config) {
      configure_service(config);
   })
   .plugin(make_http_plugin_descriptor());

std::unique_ptr<fcl::app::application_shell> app = std::move(builder).build();
app->configure(document);
boost::asio::awaitable<void> start_then_stop(fcl::app::application_shell& app) {
   co_await app.startup();
   app.request_stop();
   co_await app.shutdown();
}
```

Buildable examples:

- [examples/app/application_lifecycle.cpp](../../examples/app/application_lifecycle.cpp)
  shows `application_shell`, typed ports, config, lifecycle signals,
  diagnostics and POSIX signal bridge.
- [examples/app/application_builder.cpp](../../examples/app/application_builder.cpp)
  shows builder syntax that still returns an `application_shell`.
- [examples/app/exception_logging.cpp](../../examples/app/exception_logging.cpp)
  shows exception capture routed into `fcl_log` without making
  `fcl_exception` depend on logging.

## Failure And Rollback

- Configure failures stop before any runtime side effects should begin.
- Initialize/startup failures are recorded in diagnostics.
- Already-started plugins are shut down where possible.
- Shutdown must be idempotent enough for tests and process boundaries.

## Boundaries

- `fcl_asio` imports no app/config/network/TUI code.
- `fcl_app` can depend on `fcl_config`, but not `fcl_yaml`,
  `fcl_program_options` or parser backends.
- Events and signals are diagnostics/lifecycle surfaces. They are not hidden
  business-flow transport.
- Ports are typed boundaries; stringly-typed event buses are not a replacement
  for ports.

## Donor Decisions

Accepted:

- Boost.Asio executor/timer/coroutine model.
- FC-style numeric priority as a donor idea, but not FC runtime primitives.
- Container/service-style deterministic startup and rollback.
- Observable diagnostics from operations-oriented systems.

Rejected:

- `fc::thread`, `fc::future`, `fc::promise` and direct scheduling APIs.
- `std::async` as product-grade daemon/runtime worker substrate.
- Unbounded event subscribers or "fire and forget" detached work.
- CLI parser types inside plugin core.

## Verification

- `test_fcl_asio`: priority ordering, delayed tasks, cancellation, queue bounds
  and shutdown.
- `test_fcl_app`: config collection, configure-before-initialize, dependency
  ordering, rollback, diagnostics and reverse shutdown.
