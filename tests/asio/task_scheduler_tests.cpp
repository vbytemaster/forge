#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

#include <boost/test/unit_test.hpp>

#include <boost/asio/awaitable.hpp>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.asio.task_scheduler;

namespace {

using fcl::asio::priority;
using fcl::asio::scheduled_task;
using fcl::asio::task_handle;
using fcl::asio::task_scheduler;
using fcl::asio::task_scheduler_options;

void wait_task(fcl::asio::runtime& runtime, const task_handle& handle) {
   fcl::asio::blocking::run(runtime, handle.wait());
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

   auto gate = scheduler.submit(scheduled_task{
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
   auto low = scheduler.submit(scheduled_task{.priority = priority{10}, .name = "low", .work = record(4)});
   auto high_a = scheduler.submit(scheduled_task{.priority = priority{50}, .name = "high-a", .work = record(2)});
   auto high_b = scheduler.submit(scheduled_task{.priority = priority{50}, .name = "high-b", .work = record(3)});

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

   auto early = scheduler.submit_after(scheduled_task{.priority = priority{1}, .name = "early", .work = record(1)},
                                       std::chrono::milliseconds{5});
   auto late = scheduler.submit_after(scheduled_task{.priority = priority{1}, .name = "late", .work = record(2)},
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

   auto canceled = scheduler.submit_after(scheduled_task{.priority = priority{1}, .name = "cancel", .work = [] {}},
                                          std::chrono::seconds{1});
   BOOST_CHECK(canceled.cancel());
   BOOST_CHECK(canceled.cancel_requested());
   BOOST_CHECK_THROW(wait_task(runtime, canceled), fcl::asio::exceptions::canceled);

   auto bounded_runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto bounded =
       task_scheduler{bounded_runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 1}};
   auto queued = bounded.submit_after(scheduled_task{.priority = priority{1}, .name = "queued", .work = [] {}},
                                      std::chrono::seconds{1});
   auto rejected = bounded.submit_after(scheduled_task{.priority = priority{1}, .name = "rejected", .work = [] {}},
                                        std::chrono::seconds{1});

   BOOST_CHECK_THROW(wait_task(bounded_runtime, rejected), fcl::asio::exceptions::rejected);
   BOOST_CHECK_EQUAL(bounded.metrics().rejected, 1U);
   static_cast<void>(queued.cancel());
   bounded.stop();
}

BOOST_AUTO_TEST_CASE(task_scheduler_shutdown_cancels_pending_work) {
   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 1}};
   auto scheduler = task_scheduler{runtime, task_scheduler_options{.max_active_tasks = 1, .max_pending_tasks = 4}};
   auto delayed = scheduler.submit_after(scheduled_task{.priority = priority{1}, .name = "delayed", .work = [] {}},
                                         std::chrono::seconds{10});

   scheduler.stop();

   BOOST_CHECK_THROW(wait_task(runtime, delayed), fcl::asio::exceptions::canceled);
   BOOST_CHECK(scheduler.metrics().stopped);
   BOOST_CHECK_EQUAL(scheduler.metrics().pending, 0U);
}
