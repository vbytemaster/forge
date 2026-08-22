module;

#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/compat/move_only_function.hpp>

module forge.net.p2p.node;

import forge.asio.notification;
import forge.net.p2p.exceptions;

#include "details/cancellation_latch.hxx"
#include "details/lifecycle_tracker.hxx"
#include "details/lifecycle_wakeup.hxx"
#include "details/topology_dht_fanout.hxx"
#include "details/worker_stop_bridge.hxx"

namespace forge::net::p2p::detail::topology_dht_fanout {
namespace {

void reach_test_failpoint(const test_hooks& hooks, test_stage stage) {
   if (hooks.reach != nullptr) {
      hooks.reach(hooks.context, stage);
   }
}

void complete_worker(const std::shared_ptr<worker_batch>& batch,
                     const std::shared_ptr<lifecycle_tracker::operation>& operation,
                     const std::shared_ptr<std::atomic_bool>& settled,
                     const test_hooks& hooks, std::exception_ptr error) noexcept {
   try {
      reach_test_failpoint(hooks, test_stage::before_worker_completion);
   } catch (...) {
      if (!error) {
         error = std::current_exception();
      }
   }
   if (settled->exchange(true, std::memory_order_acq_rel)) {
      return;
   }
   // The shared stop bridge joins both branches before this completion handler
   // runs, so no composed cancellation remains in flight.
   batch->complete(std::move(error));
   if (operation) {
      operation->release();
   }
}

void complete_unlaunched_worker(const std::shared_ptr<worker_batch>& batch,
                                const std::shared_ptr<lifecycle_tracker::operation>& operation,
                                const std::shared_ptr<std::atomic_bool>& settled) noexcept {
   if (settled->exchange(true, std::memory_order_acq_rel)) {
      return;
   }
   batch->complete({});
   if (operation) {
      operation->release();
   }
}

} // namespace

worker_batch::worker_batch(std::size_t)
    : completed_{std::make_shared<lifecycle_wakeup>()}, cancellation_{std::make_shared<cancellation_latch>()} {}

void worker_batch::publish() {
   const auto lock = std::scoped_lock{mutex_};
   ++remaining_workers_;
}

void worker_batch::complete(std::exception_ptr error) noexcept {
   try {
      auto notify = false;
      {
         const auto lock = std::scoped_lock{mutex_};
         if (error && !first_failure_) {
            first_failure_ = std::move(error);
         }
         if (remaining_workers_ != 0) {
            --remaining_workers_;
         }
         notify = launches_complete_ && remaining_workers_ == 0 && !completion_notified_;
         completion_notified_ = completion_notified_ || notify;
      }
      if (notify) {
         completed_->notify();
      }
   } catch (...) {
      // Completion handlers must never escape into the executor.
   }
}

void worker_batch::cancel() noexcept {
   cancellation_->request_stop();
}

void worker_batch::close_launches() noexcept {
   try {
      auto notify = false;
      {
         const auto lock = std::scoped_lock{mutex_};
         launches_complete_ = true;
         notify = remaining_workers_ == 0 && !completion_notified_;
         completion_notified_ = completion_notified_ || notify;
      }
      if (notify) {
         completed_->notify();
      }
   } catch (...) {
   }
}

std::shared_ptr<cancellation_latch> worker_batch::cancellation() const noexcept {
   return cancellation_;
}

std::exception_ptr worker_batch::first_failure() const noexcept {
   try {
      const auto lock = std::scoped_lock{mutex_};
      return first_failure_;
   } catch (...) {
      return {};
   }
}

boost::asio::awaitable<void> worker_batch::async_join(test_hooks hooks) {
   while (true) {
      const auto observed = completed_->epoch();
      {
         const auto lock = std::scoped_lock{mutex_};
         if (remaining_workers_ == 0) {
            co_return;
         }
      }
      reach_test_failpoint(hooks, test_stage::before_join_wait);
      static_cast<void>(co_await completed_->async_wait(observed));
   }
}

boost::asio::awaitable<std::exception_ptr> async_run(request value) {
   auto batch = value.batch ? std::move(value.batch) : std::make_shared<worker_batch>(value.workers);
   auto failure = std::exception_ptr{};
   auto parent_subscription = cancellation_latch::subscription{};
   auto child_subscriptions = std::vector<cancellation_latch::subscription>{};
   try {
      // All potentially allocating child cancellation ownership is prepared
      // before worker publication. The batch itself only exposes this sticky
      // latch, never a raw Boost cancellation signal.
      child_subscriptions.reserve(value.workers);
      parent_subscription = cancellation_latch::subscribe(value.cancellation, [batch] noexcept { batch->cancel(); });
      for (auto worker = std::size_t{}; worker < value.workers; ++worker) {
         auto published = false;
         auto launched = false;
         auto operation = std::shared_ptr<lifecycle_tracker::operation>{};
         auto settled = std::shared_ptr<std::atomic_bool>{};
         try {
            reach_test_failpoint(value.hooks, test_stage::before_worker_setup);
            auto stop = std::make_shared<worker_stop_bridge>();
            auto child_subscription = cancellation_latch::subscribe(
                batch->cancellation(), [stop] noexcept { stop->request_stop(); });
            auto task = value.worker;
            auto executor = value.executor;
            auto lifecycle_stop = std::shared_ptr<lifecycle_stop_source>{};
            if (value.lifecycle != nullptr) {
               auto tracked = value.lifecycle->track();
               if (!tracked.active()) {
                  FORGE_THROW_EXCEPTION(exceptions::closed, "P2P topology DHT worker lifecycle is stopped");
               }
               operation = std::make_shared<lifecycle_tracker::operation>(std::move(tracked));
               executor = operation->executor();
               lifecycle_stop = operation->stop_source();
            }
            auto worker_executor = boost::asio::make_strand(std::move(executor));
            settled = std::make_shared<std::atomic_bool>(false);
            // A stop from any thread can only notify this bridge; the worker
            // strand creates and joins all cancellation operations internally.
            child_subscriptions.emplace_back(std::move(child_subscription));
            batch->publish();
            published = true;
            reach_test_failpoint(value.hooks, test_stage::before_worker_spawn);
            boost::asio::co_spawn(
                worker_executor,
                [task = std::move(task), stop = std::move(stop), lifecycle_stop, hooks = value.hooks]() mutable
                    -> boost::asio::awaitable<void> {
                   auto options = worker_stop_bridge_options{
                       .lifecycle_stop = std::move(lifecycle_stop),
                       .before_stop_wait = [hooks] { reach_test_failpoint(hooks, test_stage::before_worker_stop_wait); },
                   };
                   co_await async_run_with_stop_bridge(std::move(stop), std::move(task), std::move(options));
                },
                boost::asio::bind_executor(worker_executor,
                                           [batch, operation, settled, hooks = value.hooks](std::exception_ptr error) noexcept {
                                              complete_worker(batch, operation, settled, hooks, std::move(error));
                                           }));
            launched = true;
         } catch (...) {
            if (published && !launched) {
               complete_unlaunched_worker(batch, operation, settled);
            } else if (launched) {
               batch->cancel();
            }
            throw;
         }
      }
   } catch (...) {
      failure = std::current_exception();
      batch->cancel();
   }

   batch->close_launches();
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   auto join_hooks = value.hooks;
   while (true) {
      try {
         co_await batch->async_join(join_hooks);
         break;
      } catch (...) {
         if (!failure) {
            failure = std::current_exception();
         }
         batch->cancel();
         // A test failpoint may abort the first wait. Retrying without it is
         // required before this transaction may release lifecycle ownership.
         join_hooks = {};
      }
   }
   if (!failure) {
      failure = batch->first_failure();
   }
   co_return failure;
}

} // namespace forge::net::p2p::detail::topology_dht_fanout
