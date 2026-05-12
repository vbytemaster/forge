# fcl_asio

`fcl_asio` owns the shared async runtime primitives used by FCL networking and
applications. It wraps Boost.Asio with explicit runtime ownership, blocking
boundaries and a priority task scheduler.

## When To Use

- A library needs an owned `boost::asio::io_context` runtime.
- Background work needs bounded queues, cancellation and deterministic shutdown.
- Blocking code must be isolated from coroutine-first paths.

## When Not To Use

- Do not use it as a generic global job system.
- Do not encode product priority names here; `fcl::asio::priority` is numeric.
- Do not expose `std::future` as public async API. FCL async APIs use
  `boost::asio::awaitable<T>`.

## Public Modules

- `fcl.asio.runtime` — owned `io_context` and worker threads.
- `fcl.asio.blocking` — explicit blocking boundary helpers.
- `fcl.asio.task_scheduler` — bounded priority scheduler and task handles.

Target: `fcl_asio`.

Dependencies: Boost.Asio and threads.

## Examples

### Own A Runtime

```cpp
import fcl.asio.runtime;

auto runtime = fcl::asio::runtime{{.worker_threads = 2, .thread_name = "worker"}};
auto& io = runtime.context();
runtime.stop();
```

### Submit Priority Work

```cpp
import fcl.asio.task_scheduler;

auto scheduler = fcl::asio::task_scheduler{runtime, {.max_active_tasks = 4}};
auto handle = scheduler.submit({
   .priority = fcl::asio::priority{100},
   .name = "metadata-refresh",
   .work = [] { refresh_metadata(); },
});

co_await handle.wait();
```

### Cancel Delayed Work

```cpp
auto handle = scheduler.submit_after(
   {.priority = fcl::asio::priority{0}, .name = "retry", .work = [] { retry(); }},
   std::chrono::milliseconds{250});

handle.cancel();
```

## Backpressure And Shutdown

`task_scheduler_options::max_pending_tasks` is a correctness knob. Saturated
queues reject work instead of growing without bound. `stop()` cancels pending
work and lets tests verify deterministic shutdown behavior.

## Typical Mistakes

- Do not sleep/poll inside runtime loops; use timers and scheduler handles.
- Do not let a task capture references whose lifetime is shorter than the
  scheduler queue.
- Do not hide blocking I/O in coroutine paths without the blocking boundary.

## Tests

`test_fcl_asio` covers priority/FIFO ordering, delayed execution, cancellation,
queue saturation and shutdown cancellation.
