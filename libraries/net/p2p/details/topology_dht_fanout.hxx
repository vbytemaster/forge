#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>

namespace forge::net::p2p {

class cancellation_latch;

namespace detail {
class lifecycle_wakeup;
class lifecycle_tracker;
class worker_terminal_owner;
}

namespace detail::topology_dht_fanout {

enum class test_stage {
   before_worker_setup,
   before_worker_spawn,
   before_worker_stop_wait,
   before_worker_completion,
   before_join_wait,
};

struct test_hooks {
   void* context = nullptr;
   void (*reach)(void*, test_stage) = nullptr;
};

class worker_batch;

struct request {
   boost::asio::any_io_executor executor;
   std::size_t workers = 0;
   std::shared_ptr<cancellation_latch> cancellation;
   std::function<boost::asio::awaitable<void>(std::shared_ptr<worker_terminal_owner>)> worker;
   std::shared_ptr<worker_batch> batch;
   lifecycle_tracker* lifecycle = nullptr;
   test_hooks hooks{};
};

class worker_batch final {
 public:
   explicit worker_batch(std::size_t capacity);

   void publish();
   void complete(std::exception_ptr error) noexcept;
   void cancel() noexcept;
   void close_launches() noexcept;

   [[nodiscard]] std::shared_ptr<cancellation_latch> cancellation() const noexcept;
   [[nodiscard]] std::exception_ptr first_failure() const noexcept;

   boost::asio::awaitable<void> async_join(test_hooks hooks);

 private:
   mutable std::mutex mutex_;
   std::shared_ptr<detail::lifecycle_wakeup> completed_;
   std::shared_ptr<cancellation_latch> cancellation_;
   std::size_t remaining_workers_ = 0;
   bool launches_complete_ = false;
   bool completion_notified_ = false;
   std::exception_ptr first_failure_;
};

// Launches topology DHT workers as one transaction. A setup failure cancels
// every published child and waits for all their completion handlers before
// returning the captured failure.
boost::asio::awaitable<std::exception_ptr> async_run(request value);

} // namespace detail::topology_dht_fanout
} // namespace forge::net::p2p
