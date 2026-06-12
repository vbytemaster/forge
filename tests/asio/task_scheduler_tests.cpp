#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

#include <boost/test/unit_test.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.asio.task_scheduler;

namespace {

using fcl::asio::awaitable_task;
using fcl::asio::priority;
using fcl::asio::task;
using fcl::asio::task_context;
using fcl::asio::task_handle;
using fcl::asio::task_scheduler;
using fcl::asio::task_scheduler_options;

void wait_task(fcl::asio::runtime& runtime, const task_handle& handle) {
   fcl::asio::blocking::run(runtime, handle.wait());
}

void wait_until_true(const std::atomic_bool& value) {
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (!value.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
   }
   BOOST_REQUIRE(value.load(std::memory_order_acquire));
}

#if defined(__APPLE__) || defined(__linux__)
boost::asio::awaitable<std::string> current_thread_name() {
   char name[64] = {};
   if (pthread_getname_np(pthread_self(), name, sizeof(name)) != 0) {
      co_return std::string{};
   }
   co_return std::string{name};
}
#endif

} // namespace

BOOST_AUTO_TEST_CASE(runtime_applies_custom_worker_thread_name_when_observable) {
#if defined(__APPLE__) || defined(__linux__)
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1, .thread_name = "fcltest"}};
   const auto name = fcl::asio::blocking::run(runtime, current_thread_name());
   BOOST_TEST(name == "fcltest");
#else
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1, .thread_name = "fcltest"}};
   runtime.stop();
#endif
}

BOOST_AUTO_TEST_CASE(task_scheduler_orders_by_numeric_priority_then_fifo) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 8}};

   auto gate_mutex = std::mutex{};
   auto gate_cv = std::condition_variable{};
   auto release_gate = false;
   auto order_mutex = std::mutex{};
   auto order = std::vector<int>{};
   auto record = [&](int value) {
      return [&, value] {
         const auto lock = std::scoped_lock{order_mutex};
         order.push_back(value);
      };
   };

   auto gate = scheduler.submit(task{
       .priority = priority{100},
       .name = "gate",
       .work =
           [&] {
              auto lock = std::unique_lock{gate_mutex};
              gate_cv.wait(lock, [&] { return release_gate; });
              lock.unlock();
              record(0)();
           },
   });
   auto low = scheduler.submit(task{.priority = priority{10}, .name = "low", .work = record(4)});
   auto high_a = scheduler.submit(awaitable_task{
      .priority = priority{50},
      .name = "high-a",
      .work =
         [&](task_context&) -> boost::asio::awaitable<void> {
         record(2)();
         co_return;
      },
   });
   auto high_b = scheduler.submit(task{.priority = priority{50}, .name = "high-b", .work = record(3)});

   {
      const auto lock = std::scoped_lock{gate_mutex};
      release_gate = true;
   }
   gate_cv.notify_all();

   wait_task(runtime, gate);
   wait_task(runtime, high_a);
   wait_task(runtime, high_b);
   wait_task(runtime, low);

   const auto expected = std::vector<int>{0, 2, 3, 4};
   BOOST_REQUIRE_EQUAL(order.size(), expected.size());
   BOOST_CHECK_EQUAL_COLLECTIONS(order.begin(), order.end(), expected.begin(), expected.end());
   BOOST_CHECK_EQUAL(scheduler.metrics().completed, 4U);
}

BOOST_AUTO_TEST_CASE(task_scheduler_runs_delayed_tasks_when_due) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};
   auto order_mutex = std::mutex{};
   auto order = std::vector<int>{};
   auto record = [&](int value) {
      return [&, value] {
         const auto lock = std::scoped_lock{order_mutex};
         order.push_back(value);
      };
   };

   auto early = scheduler.submit_after(task{.priority = priority{1}, .name = "early", .work = record(1)},
                                       std::chrono::milliseconds{5});
   auto late = scheduler.submit_after(task{.priority = priority{1}, .name = "late", .work = record(2)},
                                      std::chrono::milliseconds{25});

   wait_task(runtime, early);
   wait_task(runtime, late);

   const auto expected = std::vector<int>{1, 2};
   BOOST_REQUIRE_EQUAL(order.size(), expected.size());
   BOOST_CHECK_EQUAL_COLLECTIONS(order.begin(), order.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(task_scheduler_cancels_pending_and_rejects_saturated_queue) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 2}};

   auto canceled = scheduler.submit_after(task{.priority = priority{1}, .name = "cancel", .work = [] {}},
                                          std::chrono::seconds{1});
   BOOST_CHECK(canceled.cancel());
   BOOST_CHECK(canceled.cancel_requested());
   BOOST_CHECK_THROW(wait_task(runtime, canceled), fcl::asio::exceptions::canceled);

   auto bounded_runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto bounded =
       task_scheduler{bounded_runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 1}};
   auto queued = bounded.submit_after(task{.priority = priority{1}, .name = "queued", .work = [] {}},
                                      std::chrono::seconds{1});
   auto rejected = bounded.submit_after(task{.priority = priority{1}, .name = "rejected", .work = [] {}},
                                        std::chrono::seconds{1});

   BOOST_CHECK_THROW(wait_task(bounded_runtime, rejected), fcl::asio::exceptions::rejected);
   BOOST_CHECK_EQUAL(bounded.metrics().rejected, 1U);
   static_cast<void>(queued.cancel());
   bounded.stop();
}

BOOST_AUTO_TEST_CASE(task_scheduler_shutdown_cancels_pending_work) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};
   auto delayed = scheduler.submit_after(task{.priority = priority{1}, .name = "delayed", .work = [] {}},
                                         std::chrono::seconds{10});

   scheduler.stop();

   BOOST_CHECK_THROW(wait_task(runtime, delayed), fcl::asio::exceptions::canceled);
   BOOST_CHECK(scheduler.metrics().stopped);
   BOOST_CHECK_EQUAL(scheduler.metrics().pending, 0U);
}

BOOST_AUTO_TEST_CASE(task_scheduler_runs_awaitable_tasks) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};
   auto ran = false;

   auto handle = scheduler.submit(awaitable_task{
      .priority = priority{1},
      .name = "awaitable",
      .work =
         [&](task_context&) -> boost::asio::awaitable<void> {
         ran = true;
         co_return;
      },
   });

   wait_task(runtime, handle);
   BOOST_CHECK(ran);
   BOOST_CHECK_EQUAL(scheduler.metrics().completed, 1U);
}

BOOST_AUTO_TEST_CASE(task_scheduler_runs_delayed_awaitable_tasks_when_due) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};
   auto order_mutex = std::mutex{};
   auto order = std::vector<int>{};
   auto record = [&](int value) -> boost::asio::awaitable<void> {
      const auto lock = std::scoped_lock{order_mutex};
      order.push_back(value);
      co_return;
   };

   auto early = scheduler.submit_after(
      awaitable_task{
         .priority = priority{1},
         .name = "early",
         .work =
            [&](task_context&) -> boost::asio::awaitable<void> {
            co_await record(1);
         },
      },
      std::chrono::milliseconds{5});
   auto late = scheduler.submit_after(
      awaitable_task{
         .priority = priority{1},
         .name = "late",
         .work =
            [&](task_context&) -> boost::asio::awaitable<void> {
            co_await record(2);
         },
      },
      std::chrono::milliseconds{25});

   wait_task(runtime, early);
   wait_task(runtime, late);

   const auto expected = std::vector<int>{1, 2};
   BOOST_REQUIRE_EQUAL(order.size(), expected.size());
   BOOST_CHECK_EQUAL_COLLECTIONS(order.begin(), order.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(task_scheduler_cancels_pending_awaitable_tasks) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};
   auto ran = false;

   auto handle = scheduler.submit_after(
      awaitable_task{
         .priority = priority{1},
         .name = "pending-cancel",
         .work =
            [&](task_context&) -> boost::asio::awaitable<void> {
            ran = true;
            co_return;
         },
      },
      std::chrono::seconds{1});

   BOOST_CHECK(handle.cancel());
   BOOST_CHECK_THROW(wait_task(runtime, handle), fcl::asio::exceptions::canceled);
   BOOST_CHECK(!ran);
}

BOOST_AUTO_TEST_CASE(task_scheduler_running_awaitable_tasks_observe_cancel_request) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};
   auto started = std::atomic_bool{false};
   auto observed_cancel = false;

   auto handle = scheduler.submit(awaitable_task{
      .priority = priority{1},
      .name = "running-cancel",
      .work =
         [&](task_context& context) -> boost::asio::awaitable<void> {
         started.store(true, std::memory_order_release);
         while (!context.cancel_requested()) {
            auto executor = co_await boost::asio::this_coro::executor;
            auto timer = boost::asio::steady_timer{executor, std::chrono::milliseconds{1}};
            co_await timer.async_wait(boost::asio::use_awaitable);
         }
         observed_cancel = true;
      },
   });

   wait_until_true(started);
   BOOST_CHECK(handle.cancel());
   wait_task(runtime, handle);
   BOOST_CHECK(observed_cancel);
}

BOOST_AUTO_TEST_CASE(task_scheduler_throw_if_cancel_requested_marks_awaitable_canceled) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};
   auto started = std::atomic_bool{false};

   auto handle = scheduler.submit(awaitable_task{
      .priority = priority{1},
      .name = "throw-cancel",
      .work =
         [&](task_context& context) -> boost::asio::awaitable<void> {
         started.store(true, std::memory_order_release);
         while (!context.cancel_requested()) {
            auto executor = co_await boost::asio::this_coro::executor;
            auto timer = boost::asio::steady_timer{executor, std::chrono::milliseconds{1}};
            co_await timer.async_wait(boost::asio::use_awaitable);
         }
         context.throw_if_cancel_requested();
      },
   });

   wait_until_true(started);
   BOOST_CHECK(handle.cancel());
   BOOST_CHECK_THROW(wait_task(runtime, handle), fcl::asio::exceptions::canceled);
   BOOST_CHECK_EQUAL(scheduler.metrics().canceled, 1U);
}

BOOST_AUTO_TEST_CASE(task_scheduler_reports_awaitable_exceptions) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};

   auto handle = scheduler.submit(awaitable_task{
      .priority = priority{1},
      .name = "throw",
      .work =
         [](task_context&) -> boost::asio::awaitable<void> {
         throw std::runtime_error{"awaitable failed"};
      },
   });

   BOOST_CHECK_THROW(wait_task(runtime, handle), std::runtime_error);
   BOOST_CHECK_EQUAL(scheduler.metrics().failed, 1U);
}

BOOST_AUTO_TEST_CASE(task_scheduler_rejects_saturated_awaitable_queue) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 1}};

   auto queued = scheduler.submit_after(
      awaitable_task{
         .priority = priority{1},
         .name = "queued",
         .work = [](task_context&) -> boost::asio::awaitable<void> {
            co_return;
         },
      },
      std::chrono::seconds{1});
   auto rejected = scheduler.submit_after(
      awaitable_task{
         .priority = priority{1},
         .name = "rejected",
         .work = [](task_context&) -> boost::asio::awaitable<void> {
            co_return;
         },
      },
      std::chrono::seconds{1});

   BOOST_CHECK_THROW(wait_task(runtime, rejected), fcl::asio::exceptions::rejected);
   BOOST_CHECK_EQUAL(scheduler.metrics().rejected, 1U);
   static_cast<void>(queued.cancel());
   scheduler.stop();
}

BOOST_AUTO_TEST_CASE(task_scheduler_stop_cancels_pending_and_waits_for_active_awaitable_tasks) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};
   auto started = std::atomic_bool{false};
   auto allow_finish = std::atomic_bool{false};

   auto active = scheduler.submit(awaitable_task{
      .priority = priority{1},
      .name = "active",
      .work =
         [&](task_context&) -> boost::asio::awaitable<void> {
         started.store(true, std::memory_order_release);
         while (!allow_finish.load(std::memory_order_acquire)) {
            auto executor = co_await boost::asio::this_coro::executor;
            auto timer = boost::asio::steady_timer{executor, std::chrono::milliseconds{1}};
            co_await timer.async_wait(boost::asio::use_awaitable);
         }
      },
   });
   auto pending = scheduler.submit_after(
      awaitable_task{
         .priority = priority{1},
         .name = "pending",
         .work = [](task_context&) -> boost::asio::awaitable<void> {
            co_return;
         },
      },
      std::chrono::seconds{1});

   wait_until_true(started);
   auto stopper = std::thread{[&] { scheduler.stop(); }};
   std::this_thread::sleep_for(std::chrono::milliseconds{10});
   allow_finish.store(true, std::memory_order_release);
   stopper.join();

   wait_task(runtime, active);
   BOOST_CHECK_THROW(wait_task(runtime, pending), fcl::asio::exceptions::canceled);
   BOOST_CHECK(scheduler.metrics().stopped);
}

BOOST_AUTO_TEST_CASE(task_scheduler_supports_host_owned_awaitable_reschedule_loop) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};
   auto passes = 0;

   while (passes < 3) {
      auto handle = scheduler.submit_after(
         awaitable_task{
            .priority = priority{-50},
            .name = "scrub-pass",
            .work =
               [&](task_context&) -> boost::asio::awaitable<void> {
               ++passes;
               co_return;
            },
         },
         std::chrono::milliseconds{1});
      wait_task(runtime, handle);
   }

   BOOST_CHECK_EQUAL(passes, 3);
   BOOST_CHECK_EQUAL(scheduler.metrics().completed, 3U);
}
