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

### Bridge Coroutine Code To A Blocking Entrypoint

Use `blocking::run` at the edge of a command-line tool, test, migration or
small utility. Do not push it into reusable async APIs.

```cpp
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>

import fcl.asio.blocking;
import fcl.asio.runtime;

boost::asio::awaitable<int> calculate_after_delay() {
   boost::asio::any_io_executor executor = co_await boost::asio::this_coro::executor;
   auto timer = boost::asio::steady_timer{executor, std::chrono::milliseconds{25}};
   co_await timer.async_wait(boost::asio::use_awaitable);
   co_return 42;
}

auto runtime = fcl::asio::runtime{};
auto value = fcl::asio::blocking::run(runtime, calculate_after_delay());
```

### Bound A Blocking Wait With Timeout

`run_for` is useful for tests and operator probes where a missing callback should
be reported as a timeout, not as an infinite hang.

```cpp
import fcl.asio.blocking;

auto completed = fcl::asio::blocking::run_for(
   runtime,
   wait_for_readiness_probe(),
   std::chrono::seconds{5});

if (!completed) {
   report_unavailable("readiness probe timed out");
}
```

### Submit Priority Work

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.asio.task_scheduler;

boost::asio::awaitable<void> submit_metadata_refresh(fcl::asio::runtime& runtime) {
   auto scheduler = fcl::asio::task_scheduler{runtime, {.max_active_tasks = 4}};
   auto refresh = scheduler.submit({
      .priority = fcl::asio::priority{100},
      .name = "metadata-refresh",
      .work = [] { refresh_metadata(); },
   });

   co_await refresh.wait();
}
```

### Isolate Blocking Work Behind The Scheduler

The scheduler is the boundary for short blocking functions that should not run
inline on a coroutine hot path. The reusable library still exposes a coroutine
wait handle instead of `std::future`.

```cpp
#include <boost/asio/awaitable.hpp>

import fcl.asio.task_scheduler;

boost::asio::awaitable<void> refresh_index(fcl::asio::task_scheduler& scheduler) {
   auto index_job = scheduler.submit({
      .priority = fcl::asio::priority{25},
      .name = "index-refresh",
      .work = [] {
         rebuild_small_index_from_disk();
      },
   });

   co_await index_job.wait();
}
```

Do not capture stack references in `.work` unless you can prove the work will
finish before the referenced object goes away.

### Handle Queue Backpressure

`max_pending_tasks` is part of correctness. Callers must handle rejection
instead of assuming the queue can grow forever.

```cpp
boost::asio::awaitable<void> run_small_job(fcl::asio::runtime& runtime) {
   auto scheduler = fcl::asio::task_scheduler{
      runtime,
      {.max_active_tasks = 1, .max_pending_tasks = 2},
   };

   auto accepted = scheduler.submit({
      .priority = fcl::asio::priority{0},
      .name = "small-job",
      .work = [] { do_small_job(); },
   });

   try {
      co_await accepted.wait();
   } catch (const fcl::asio::exceptions::rejected& error) {
      report_busy(error.what()); // for example: scheduler queue is full
   }
}
```

Queue rejection is a product signal. A daemon should surface a typed busy or
backpressure error, not spawn an unbounded fallback thread to “make progress”.

### Use Numeric Priorities Without Product Vocabulary

The scheduler only knows numbers. A product can define its own named constants
near the component that owns those meanings.

```cpp
namespace priorities {
   inline constexpr auto foreground = fcl::asio::priority{500};
   inline constexpr auto background = fcl::asio::priority{-100};
}

auto hot = scheduler.submit({
   .priority = priorities::foreground,
   .name = "foreground-read",
   .work = [] { serve_foreground_request(); },
});

auto cold = scheduler.submit({
   .priority = priorities::background,
   .name = "background-refresh",
   .work = [] { refresh_cache_index(); },
});
```

### Cancel Delayed Work

```cpp
auto handle = scheduler.submit_after(
   {.priority = fcl::asio::priority{0}, .name = "retry", .work = [] { retry(); }},
   std::chrono::milliseconds{250});

handle.cancel();
```

### Use Timers Instead Of Poll Loops

```cpp
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

boost::asio::awaitable<void> retry_later() {
   boost::asio::any_io_executor executor = co_await boost::asio::this_coro::executor;
   auto timer = boost::asio::steady_timer{executor, std::chrono::milliseconds{200}};
   co_await timer.async_wait(boost::asio::use_awaitable);
   co_await retry_once();
}
```

This keeps cancellation and shutdown visible to the runtime. Avoid background
threads that sleep and poll shared state.

### Shut Down Deterministically

Call `stop()` when the owning service is stopping. Pending work is canceled,
running work is allowed to finish through its normal function body, and handles
can still be awaited by tests.

```cpp
boost::asio::awaitable<void> stop_with_pending_work(fcl::asio::task_scheduler& scheduler) {
   auto pending = scheduler.submit_after(
      {.priority = fcl::asio::priority{0}, .name = "slow-retry", .work = [] { retry(); }},
      std::chrono::minutes{1});

   scheduler.stop();
   co_await pending.wait();

   auto metrics = scheduler.metrics();
   assert(metrics.stopped);
   assert(metrics.canceled >= 1);
}
```

## Backpressure And Shutdown

`task_scheduler_options::max_pending_tasks` is a correctness knob. Saturated
queues reject work instead of growing without bound. `stop()` cancels pending
work and lets tests verify deterministic shutdown behavior.

## Runtime Risks And Anti-Patterns

- Do not detach raw `std::thread` workers around the scheduler. That bypasses
  cancellation, metrics and deterministic shutdown.
- Do not use `std::async` as a daemon worker pool. It has no FCL backpressure or
  lifecycle integration.
- Do not sleep in polling loops on runtime threads. Use timers, task handles and
  explicit cancellation.
- Do not call `stop()` before consumers have awaited cleanup handles. A stopped
  scheduler rejects new cleanup work by design.
- Do not capture stack references in queued tasks unless the owner awaits or
  cancels the handle before the referenced object can die.

## Typical Mistakes

- Do not sleep/poll inside runtime loops; use timers and scheduler handles.
- Do not let a task capture references whose lifetime is shorter than the
  scheduler queue.
- Do not hide blocking I/O in coroutine paths without the blocking boundary.

## Tests

`test_fcl_asio` covers priority/FIFO ordering, delayed execution, cancellation,
queue saturation and shutdown cancellation.
