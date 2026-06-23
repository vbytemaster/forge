module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

export module forge.asio.task_scheduler;

export import forge.asio.exceptions;
import forge.asio.runtime;

export namespace forge::asio {

class priority {
 public:
   explicit constexpr priority(int value = 0) noexcept : value_(value) {}

   [[nodiscard]] constexpr int value() const noexcept {
      return value_;
   }

   [[nodiscard]] static constexpr priority max() noexcept {
      return priority{10'000};
   }

   [[nodiscard]] static constexpr priority min() noexcept {
      return priority{-10'000};
   }

   [[nodiscard]] friend constexpr bool operator==(priority, priority) noexcept = default;
   [[nodiscard]] friend constexpr auto operator<=>(priority left, priority right) noexcept {
      return left.value_ <=> right.value_;
   }

 private:
   int value_ = 0;
};

struct task {
   priority priority{};
   std::string name;
   std::function<void()> work;
};

class task_context {
 public:
   [[nodiscard]] bool cancel_requested() const noexcept;
   void throw_if_cancel_requested() const;

 private:
   explicit task_context(std::atomic_bool& cancel_requested) noexcept;

   std::atomic_bool* cancel_requested_ = nullptr;

   friend class task_scheduler;
};

struct awaitable_task {
   priority priority{};
   std::string name;
   std::function<boost::asio::awaitable<void>(task_context&)> work;
};

class task_handle {
 public:
   task_handle();
   ~task_handle();

   task_handle(task_handle&&) noexcept;
   task_handle& operator=(task_handle&&) noexcept;

   task_handle(const task_handle&) = delete;
   task_handle& operator=(const task_handle&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] std::uint64_t id() const noexcept;
   [[nodiscard]] bool cancel_requested() const noexcept;
   bool cancel() noexcept;
   boost::asio::awaitable<void> wait() const;

 private:
   struct state;
   std::shared_ptr<state> state_;

   explicit task_handle(std::shared_ptr<state> state);

   friend class task_scheduler;
};

class task_scheduler {
 public:
   struct options {
      std::size_t max_blocking_tasks = 2;
      std::size_t max_awaitable_tasks = 4096;
      std::size_t max_pending_tasks = 4096;
   };

   struct metrics {
      std::uint64_t submitted = 0;
      std::uint64_t started = 0;
      std::uint64_t completed = 0;
      std::uint64_t canceled = 0;
      std::uint64_t rejected = 0;
      std::uint64_t failed = 0;
      std::size_t pending = 0;
      std::size_t running_blocking = 0;
      std::size_t running_awaitable = 0;
      bool stopped = false;
   };

   explicit task_scheduler(runtime& runtime);
   task_scheduler(runtime& runtime, options options);
   ~task_scheduler();

   task_scheduler(const task_scheduler&) = delete;
   task_scheduler& operator=(const task_scheduler&) = delete;

   task_scheduler(task_scheduler&&) = delete;
   task_scheduler& operator=(task_scheduler&&) = delete;

   task_handle submit(task value);
   task_handle submit_after(task value, std::chrono::milliseconds delay);
   task_handle submit(awaitable_task value);
   task_handle submit_after(awaitable_task value, std::chrono::milliseconds delay);

   [[nodiscard]] std::size_t pending_count() const;
   [[nodiscard]] std::size_t pending_count(priority priority) const;
   [[nodiscard]] metrics snapshot() const;
   [[nodiscard]] runtime& runtime_context() noexcept;

   void stop();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::asio
