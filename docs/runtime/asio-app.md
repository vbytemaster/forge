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
  -> fcl_app::application_runtime
  -> plugins configured through component_view
  -> fcl_asio runtime / task_scheduler for async work
```

`fcl_app` consumes config documents and component views. It does not parse
files, environment or command lines. That split keeps application core testable:
tests can feed a `config::document` directly without building argv/YAML fixtures.

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

1. plugins are constructed;
2. config descriptors are collected;
3. config document is applied through `configure(component_view)`;
4. plugins initialize in dependency order;
5. plugins start;
6. `request_stop()` is issued synchronously;
7. plugins shut down in reverse order.

All heavy lifecycle methods return `boost::asio::awaitable<void>`. `request_stop`
is intentionally synchronous/noexcept: it signals intent, it does not perform
cleanup.

## Integration Example

```cpp
auto runtime = fcl::asio::runtime{{.worker_threads = 2}};
auto scheduler = fcl::asio::task_scheduler{runtime, {.max_active_tasks = 4}};
auto ports = fcl::app::port_registry{};
auto signals = fcl::app::signal_bus{};
auto events = fcl::app::event_bus{};
auto diagnostics = fcl::app::diagnostics_store{};
auto context = fcl::app::plugin_context{scheduler, ports, signals, events, &diagnostics};

auto app = fcl::app::application_runtime{context, std::move(plugins), &diagnostics};
auto registry = app.describe_config();
auto cli = fcl::program_options::parse(argc, argv, registry);

co_await app.configure(cli.document);
co_await app.initialize();
co_await app.startup();
```

The program shell owns CLI/YAML/JSON adapters. The `application_runtime` sees
only an already-merged `config::document`.

Buildable examples:

- [examples/app/application_lifecycle.cpp](../../examples/app/application_lifecycle.cpp)
  shows `application_base`, typed ports, config, lifecycle signals,
  diagnostics and POSIX signal bridge.
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
