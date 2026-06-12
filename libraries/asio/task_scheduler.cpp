module;

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module fcl.asio.task_scheduler;

import fcl.asio.exceptions;

namespace fcl::asio {
namespace {

std::exception_ptr make_error(exceptions::code code, std::string message) {
   switch (code) {
   case exceptions::code::invalid_state:
      return std::make_exception_ptr(exceptions::invalid_state{std::move(message)});
   case exceptions::code::invalid_options:
      return std::make_exception_ptr(exceptions::invalid_options{std::move(message)});
   case exceptions::code::canceled:
      return std::make_exception_ptr(exceptions::canceled{std::move(message)});
   case exceptions::code::rejected:
      return std::make_exception_ptr(exceptions::rejected{std::move(message)});
   case exceptions::code::internal:
      break;
   }
   return std::make_exception_ptr(exceptions::internal{std::move(message)});
}

} // namespace

struct task_handle::state {
   state(runtime& runtime_value, std::uint64_t task_id)
       : runtime_ptr(&runtime_value),
         completion_timer(runtime_value.context(), (std::chrono::steady_clock::time_point::max)()), id(task_id) {}

   runtime* runtime_ptr = nullptr;
   boost::asio::steady_timer completion_timer;
   std::uint64_t id = 0;
   std::atomic_bool cancel_requested = false;
   std::atomic_bool started = false;
   std::atomic_bool completed = false;
   mutable std::mutex completion_mutex;
   std::exception_ptr completion_error;

   void notify_waiters() noexcept {
      if (runtime_ptr == nullptr) {
         return;
      }
      boost::asio::post(runtime_ptr->context(), [weak = weak_self] {
         if (auto self = weak.lock()) {
            self->completion_timer.cancel();
         }
      });
   }

   bool complete_value() noexcept {
      auto expected = false;
      if (!completed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
         return false;
      }
      notify_waiters();
      return true;
   }

   bool complete_exception(std::exception_ptr error) noexcept {
      auto expected = false;
      if (!completed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
         return false;
      }
      {
         const auto lock = std::scoped_lock{completion_mutex};
         completion_error = std::move(error);
      }
      notify_waiters();
      return true;
   }

   std::exception_ptr error() const {
      const auto lock = std::scoped_lock{completion_mutex};
      return completion_error;
   }

   std::weak_ptr<state> weak_self;
};

task_handle::task_handle() = default;
task_handle::~task_handle() = default;
task_handle::task_handle(task_handle&&) noexcept = default;
task_handle& task_handle::operator=(task_handle&&) noexcept = default;

task_handle::task_handle(std::shared_ptr<state> state) : state_(std::move(state)) {}

task_context::task_context(std::atomic_bool& cancel_requested) noexcept : cancel_requested_{&cancel_requested} {}

bool task_context::cancel_requested() const noexcept {
   return cancel_requested_ != nullptr && cancel_requested_->load(std::memory_order_acquire);
}

void task_context::throw_if_cancel_requested() const {
   if (cancel_requested()) {
      throw exceptions::canceled{"scheduled task was canceled"};
   }
}

bool task_handle::valid() const noexcept {
   return state_ != nullptr;
}

std::uint64_t task_handle::id() const noexcept {
   return state_ == nullptr ? 0 : state_->id;
}

bool task_handle::cancel_requested() const noexcept {
   return state_ != nullptr && state_->cancel_requested.load(std::memory_order_acquire);
}

bool task_handle::cancel() noexcept {
   if (state_ == nullptr) {
      return false;
   }
   auto expected = false;
   const auto changed = state_->cancel_requested.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
   if (changed && !state_->started.load(std::memory_order_acquire)) {
      state_->complete_exception(make_error(exceptions::code::canceled, "scheduled task was canceled"));
   }
   return changed;
}

boost::asio::awaitable<void> task_handle::wait() const {
   if (state_ == nullptr) {
      throw exceptions::invalid_state{"task handle is empty"};
   }

   while (!state_->completed.load(std::memory_order_acquire)) {
      auto error = boost::system::error_code{};
      co_await state_->completion_timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      static_cast<void>(error);
   }

   if (auto error = state_->error()) {
      std::rethrow_exception(error);
   }
}

struct task_scheduler::impl : std::enable_shared_from_this<task_scheduler::impl> {
   struct queued_task {
      std::shared_ptr<task_handle::state> state;
      priority scheduled_priority{};
      std::string name;
      std::function<void()> work;
      std::function<boost::asio::awaitable<void>(task_context&)> awaitable_work;
      std::chrono::steady_clock::time_point ready_at = std::chrono::steady_clock::now();
      std::uint64_t sequence = 0;
   };

   struct ready_priority_less {
      bool operator()(const queued_task& left, const queued_task& right) const noexcept {
         if (left.scheduled_priority != right.scheduled_priority) {
            return left.scheduled_priority < right.scheduled_priority;
         }
         return left.sequence > right.sequence;
      }
   };

   struct delayed_time_less {
      bool operator()(const queued_task& left, const queued_task& right) const noexcept {
         if (left.ready_at != right.ready_at) {
            return left.ready_at > right.ready_at;
         }
         return left.sequence > right.sequence;
      }
   };

   impl(runtime& runtime_value, task_scheduler::options options_value)
       : runtime_ref(runtime_value), options(std::move(options_value)),
         strand(boost::asio::make_strand(runtime_ref.context())) {
      if (options.max_blocking_tasks == 0) {
         throw exceptions::invalid_options{"task scheduler requires at least one blocking task slot"};
      }
      if (options.max_awaitable_tasks == 0) {
         throw exceptions::invalid_options{"task scheduler requires at least one awaitable task slot"};
      }
      if (options.max_pending_tasks == 0) {
         throw exceptions::invalid_options{"task scheduler queue depth must be non-zero"};
      }
   }

   ~impl() {
      stop();
   }

   task_handle submit(task value, std::chrono::steady_clock::time_point ready_at) {
      if (!value.work) {
         throw exceptions::invalid_options{"scheduled task requires work"};
      }

      auto state =
          std::make_shared<task_handle::state>(runtime_ref, next_task_id.fetch_add(1, std::memory_order_relaxed));
      state->weak_self = state;

      auto queued = queued_task{
          .state = state,
          .scheduled_priority = value.priority,
          .name = std::move(value.name),
          .work = std::move(value.work),
          .awaitable_work = {},
          .ready_at = ready_at,
          .sequence = next_sequence.fetch_add(1, std::memory_order_relaxed),
      };

      return submit_queued(std::move(queued), std::move(state));
   }

   task_handle submit(awaitable_task value, std::chrono::steady_clock::time_point ready_at) {
      if (!value.work) {
         throw exceptions::invalid_options{"scheduled awaitable task requires work"};
      }

      auto state =
          std::make_shared<task_handle::state>(runtime_ref, next_task_id.fetch_add(1, std::memory_order_relaxed));
      state->weak_self = state;

      auto queued = queued_task{
          .state = state,
          .scheduled_priority = value.priority,
          .name = std::move(value.name),
          .work = {},
          .awaitable_work = std::move(value.work),
          .ready_at = ready_at,
          .sequence = next_sequence.fetch_add(1, std::memory_order_relaxed),
      };

      return submit_queued(std::move(queued), std::move(state));
   }

   task_handle submit_queued(queued_task queued, std::shared_ptr<task_handle::state> state) {
      {
         const auto lock = std::scoped_lock{mutex};
         if (stopped) {
            ++current_metrics.rejected;
            state->complete_exception(make_error(exceptions::code::rejected, "task scheduler is stopped"));
            return task_handle{std::move(state)};
         }
         if (pending_count_locked() >= options.max_pending_tasks) {
            ++current_metrics.rejected;
            state->complete_exception(make_error(exceptions::code::rejected, "task scheduler queue is full"));
            return task_handle{std::move(state)};
         }

         ++current_metrics.submitted;
         if (queued.ready_at <= std::chrono::steady_clock::now()) {
            ready_heap.push_back(std::move(queued));
            std::push_heap(ready_heap.begin(), ready_heap.end(), ready_priority_less{});
         } else {
            delayed_heap.push_back(std::move(queued));
            std::push_heap(delayed_heap.begin(), delayed_heap.end(), delayed_time_less{});
         }
         refresh_metrics_locked();
      }

      schedule_drain();
      return task_handle{std::move(state)};
   }

   std::size_t pending_count(std::optional<priority> priority_value = std::nullopt) const {
      const auto lock = std::scoped_lock{mutex};
      if (!priority_value.has_value()) {
         return pending_count_locked();
      }
      const auto matches = [priority_value](const auto& tasks) {
         return static_cast<std::size_t>(
             std::count_if(tasks.begin(), tasks.end(), [priority_value](const queued_task& task) {
                return task.scheduled_priority == *priority_value;
             }));
      };
      return matches(ready_heap) + matches(delayed_heap);
   }

   task_scheduler::metrics snapshot() const {
      const auto lock = std::scoped_lock{mutex};
      auto snapshot = current_metrics;
      snapshot.pending = pending_count_locked();
      snapshot.running_blocking = running_blocking_tasks;
      snapshot.running_awaitable = running_awaitable_tasks;
      snapshot.stopped = stopped;
      return snapshot;
   }

   void stop() {
      auto canceled = std::vector<std::shared_ptr<task_handle::state>>{};
      auto timer_to_cancel = std::shared_ptr<boost::asio::steady_timer>{};
      {
         const auto lock = std::scoped_lock{mutex};
         if (stopped) {
            return;
         }
         stopped = true;
         for (auto& task : ready_heap) {
            canceled.push_back(task.state);
         }
         for (auto& task : delayed_heap) {
            canceled.push_back(task.state);
         }
         ready_heap.clear();
         delayed_heap.clear();
         timer_to_cancel = std::move(ready_timer);
         ready_timer_deadline = (std::chrono::steady_clock::time_point::max)();
         current_metrics.canceled += canceled.size();
         refresh_metrics_locked();
      }

      for (const auto& state : canceled) {
         state->cancel_requested.store(true, std::memory_order_release);
         state->complete_exception(make_error(exceptions::code::canceled, "scheduled task was canceled"));
      }

      if (timer_to_cancel != nullptr) {
         boost::asio::post(strand, [timer = std::move(timer_to_cancel)] {
            timer->cancel();
         });
      }

      auto lock = std::unique_lock{mutex};
      active_done.wait(lock, [this] { return running_blocking_tasks == 0 && running_awaitable_tasks == 0; });
   }

   void drain() {
      auto tasks = std::vector<queued_task>{};
      {
         const auto lock = std::scoped_lock{mutex};
         auto now = std::chrono::steady_clock::now();
         move_ready_delayed_locked(now);
         if (stopped) {
            return;
         }

         while (auto task = pop_ready_locked()) {
            start_task_locked(*task);
            ++current_metrics.started;
            tasks.push_back(std::move(*task));
         }

         if (tasks.empty()) {
            arm_timer_locked();
            return;
         }
         arm_timer_locked();
         refresh_metrics_locked();
      }

      for (auto& task : tasks) {
         boost::asio::post(runtime_ref.context(), [self = shared_from_this(), task = std::move(task)]() mutable {
            self->run_task(std::move(task));
         });
      }
   }

   void run_task(queued_task task) {
      try {
         task.state->started.store(true, std::memory_order_release);
         if (task.state->cancel_requested.load(std::memory_order_acquire)) {
            throw exceptions::canceled{"scheduled task was canceled"};
         }
      } catch (...) {
         finish_task_exception(task, std::current_exception());
         return;
      }

      if (task.awaitable_work) {
         run_awaitable_task(std::move(task));
         return;
      }

      try {
         task.work();
         finish_task_value(task);
      } catch (...) {
         finish_task_exception(task, std::current_exception());
      }
   }

   void run_awaitable_task(queued_task task) {
      boost::asio::co_spawn(
          runtime_ref.context(),
          [self = shared_from_this(), task = std::move(task)]() mutable -> boost::asio::awaitable<void> {
             auto context = task_context{task.state->cancel_requested};
             try {
                context.throw_if_cancel_requested();
                co_await task.awaitable_work(context);
                self->finish_task_value(task);
             } catch (...) {
                self->finish_task_exception(task, std::current_exception());
             }
          },
          boost::asio::detached);
   }

   void finish_task_value(queued_task& task) {
      const auto completed_ok = task.state->complete_value();

      {
         const auto lock = std::scoped_lock{mutex};
         if (completed_ok) {
            ++current_metrics.completed;
         }
         complete_active_task_locked(task);
      }

      active_done.notify_all();
      schedule_drain();
   }

   void finish_task_exception(queued_task& task, std::exception_ptr error) {
      const auto completed_error = task.state->complete_exception(std::move(error));

      {
         const auto lock = std::scoped_lock{mutex};
         if (completed_error) {
            if (task.state->cancel_requested.load(std::memory_order_acquire)) {
               ++current_metrics.canceled;
            } else {
               ++current_metrics.failed;
            }
         }
         complete_active_task_locked(task);
      }

      active_done.notify_all();
      schedule_drain();
   }

   void complete_active_task_locked(const queued_task& task) {
      if (is_awaitable(task)) {
         if (running_awaitable_tasks > 0) {
            --running_awaitable_tasks;
         }
      } else if (running_blocking_tasks > 0) {
         --running_blocking_tasks;
      }
      refresh_metrics_locked();
   }

   void schedule_drain() {
      boost::asio::post(strand, [weak = weak_from_this()] {
         if (auto self = weak.lock()) {
            self->drain();
         }
      });
   }

   [[nodiscard]] static bool is_awaitable(const queued_task& task) noexcept {
      return static_cast<bool>(task.awaitable_work);
   }

   [[nodiscard]] bool can_start_locked(const queued_task& task) const noexcept {
      if (is_awaitable(task)) {
         return running_awaitable_tasks < options.max_awaitable_tasks;
      }
      return running_blocking_tasks < options.max_blocking_tasks;
   }

   void start_task_locked(const queued_task& task) noexcept {
      if (is_awaitable(task)) {
         ++running_awaitable_tasks;
      } else {
         ++running_blocking_tasks;
      }
   }

   void restore_ready_locked(std::vector<queued_task>& tasks) {
      for (auto& task : tasks) {
         ready_heap.push_back(std::move(task));
         std::push_heap(ready_heap.begin(), ready_heap.end(), ready_priority_less{});
      }
      tasks.clear();
   }

   std::optional<queued_task> pop_ready_locked() {
      auto skipped = std::vector<queued_task>{};
      while (!ready_heap.empty()) {
         std::pop_heap(ready_heap.begin(), ready_heap.end(), ready_priority_less{});
         auto task = std::move(ready_heap.back());
         ready_heap.pop_back();
         if (task.state->cancel_requested.load(std::memory_order_acquire) ||
             task.state->completed.load(std::memory_order_acquire)) {
            ++current_metrics.canceled;
            task.state->complete_exception(make_error(exceptions::code::canceled, "scheduled task was canceled"));
            refresh_metrics_locked();
            continue;
         }
         if (!can_start_locked(task)) {
            skipped.push_back(std::move(task));
            continue;
         }
         restore_ready_locked(skipped);
         refresh_metrics_locked();
         return task;
      }
      restore_ready_locked(skipped);
      return std::nullopt;
   }

   void move_ready_delayed_locked(std::chrono::steady_clock::time_point now) {
      while (!delayed_heap.empty() && delayed_heap.front().ready_at <= now) {
         std::pop_heap(delayed_heap.begin(), delayed_heap.end(), delayed_time_less{});
         auto task = std::move(delayed_heap.back());
         delayed_heap.pop_back();
         if (task.state->cancel_requested.load(std::memory_order_acquire) ||
             task.state->completed.load(std::memory_order_acquire)) {
            ++current_metrics.canceled;
            task.state->complete_exception(make_error(exceptions::code::canceled, "scheduled task was canceled"));
            continue;
         }
         ready_heap.push_back(std::move(task));
         std::push_heap(ready_heap.begin(), ready_heap.end(), ready_priority_less{});
      }
      refresh_metrics_locked();
   }

   void arm_timer_locked() {
      if (delayed_heap.empty()) {
         return;
      }

      const auto deadline = delayed_heap.front().ready_at;
      if (ready_timer != nullptr && ready_timer_deadline <= deadline) {
         return;
      }
      if (ready_timer != nullptr) {
         ready_timer->cancel();
      }
      ready_timer_deadline = deadline;
      ready_timer = std::make_shared<boost::asio::steady_timer>(strand, deadline);
      auto timer = ready_timer;
      timer->async_wait([weak = weak_from_this(), timer, deadline](const boost::system::error_code& error) {
         if (!error) {
            if (auto self = weak.lock()) {
               {
                  const auto lock = std::scoped_lock{self->mutex};
                  if (self->ready_timer == timer && self->ready_timer_deadline == deadline) {
                     self->ready_timer.reset();
                     self->ready_timer_deadline = (std::chrono::steady_clock::time_point::max)();
                  }
               }
               self->drain();
            }
         }
      });
   }

   std::size_t pending_count_locked() const {
      return ready_heap.size() + delayed_heap.size();
   }

   void refresh_metrics_locked() const {
      current_metrics.pending = pending_count_locked();
      current_metrics.running_blocking = running_blocking_tasks;
      current_metrics.running_awaitable = running_awaitable_tasks;
      current_metrics.stopped = stopped;
   }

   runtime& runtime_ref;
   task_scheduler::options options;
   decltype(boost::asio::make_strand(std::declval<boost::asio::io_context&>())) strand;
   std::shared_ptr<boost::asio::steady_timer> ready_timer;
   mutable std::mutex mutex;
   std::condition_variable active_done;
   std::vector<queued_task> ready_heap;
   std::vector<queued_task> delayed_heap;
   std::atomic_uint64_t next_task_id = 1;
   std::atomic_uint64_t next_sequence = 1;
   std::size_t running_blocking_tasks = 0;
   std::size_t running_awaitable_tasks = 0;
   std::chrono::steady_clock::time_point ready_timer_deadline = (std::chrono::steady_clock::time_point::max)();
   bool stopped = false;
   mutable task_scheduler::metrics current_metrics{};
};

task_scheduler::task_scheduler(runtime& runtime) : task_scheduler(runtime, options{}) {}

task_scheduler::task_scheduler(runtime& runtime, options options)
    : impl_(std::make_shared<impl>(runtime, std::move(options))) {}

task_scheduler::~task_scheduler() = default;

task_handle task_scheduler::submit(task value) {
   return impl_->submit(std::move(value), std::chrono::steady_clock::now());
}

task_handle task_scheduler::submit_after(task value, std::chrono::milliseconds delay) {
   return impl_->submit(std::move(value), std::chrono::steady_clock::now() + delay);
}

task_handle task_scheduler::submit(awaitable_task value) {
   return impl_->submit(std::move(value), std::chrono::steady_clock::now());
}

task_handle task_scheduler::submit_after(awaitable_task value, std::chrono::milliseconds delay) {
   return impl_->submit(std::move(value), std::chrono::steady_clock::now() + delay);
}

std::size_t task_scheduler::pending_count() const {
   return impl_->pending_count();
}

std::size_t task_scheduler::pending_count(priority priority_value) const {
   return impl_->pending_count(priority_value);
}

task_scheduler::metrics task_scheduler::snapshot() const {
   return impl_->snapshot();
}

runtime& task_scheduler::runtime_context() noexcept {
   return impl_->runtime_ref;
}

void task_scheduler::stop() {
   impl_->stop();
}

} // namespace fcl::asio
