module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

export module forge.asio.blocking;

import forge.asio.runtime;

export namespace forge::asio::blocking {

template <typename T>
   requires(!std::is_void_v<T>)
T run(forge::asio::runtime& runtime, boost::asio::awaitable<T> operation) {
   struct state {
      std::mutex mutex;
      std::condition_variable ready;
      bool done = false;
      std::optional<T> value;
      std::exception_ptr error;
   };

   auto shared = std::make_shared<state>();
   boost::asio::co_spawn(
       runtime.context(),
       [operation = std::move(operation), shared]() mutable -> boost::asio::awaitable<void> {
          auto value = std::optional<T>{};
          auto error = std::exception_ptr{};
          try {
             value.emplace(co_await std::move(operation));
          } catch (...) {
             error = std::current_exception();
          }

          {
             const auto lock = std::scoped_lock{shared->mutex};
             shared->value = std::move(value);
             shared->error = std::move(error);
             shared->done = true;
          }
          shared->ready.notify_all();
       },
       boost::asio::detached);

   auto lock = std::unique_lock{shared->mutex};
   shared->ready.wait(lock, [&] { return shared->done; });

   if (shared->error) {
      std::rethrow_exception(shared->error);
   }
   return std::move(*shared->value);
}

inline void run(forge::asio::runtime& runtime, boost::asio::awaitable<void> operation) {
   struct state {
      std::mutex mutex;
      std::condition_variable ready;
      bool done = false;
      std::exception_ptr error;
   };

   auto shared = std::make_shared<state>();
   boost::asio::co_spawn(
       runtime.context(),
       [operation = std::move(operation), shared]() mutable -> boost::asio::awaitable<void> {
          auto error = std::exception_ptr{};
          try {
             co_await std::move(operation);
          } catch (...) {
             error = std::current_exception();
          }

          {
             const auto lock = std::scoped_lock{shared->mutex};
             shared->error = std::move(error);
             shared->done = true;
          }
          shared->ready.notify_all();
       },
       boost::asio::detached);

   auto lock = std::unique_lock{shared->mutex};
   shared->ready.wait(lock, [&] { return shared->done; });

   if (shared->error) {
      std::rethrow_exception(shared->error);
   }
}

inline bool run_for(forge::asio::runtime& runtime, boost::asio::awaitable<void> operation,
                    std::chrono::milliseconds timeout) {
   struct state {
      std::mutex mutex;
      std::condition_variable ready;
      bool done = false;
      bool timed_out = false;
      std::exception_ptr error;
   };

   auto shared = std::make_shared<state>();
   auto timer = std::make_shared<boost::asio::steady_timer>(runtime.context());
   timer->expires_after(timeout);

   boost::asio::co_spawn(
       runtime.context(),
       [operation = std::move(operation), shared, timer]() mutable -> boost::asio::awaitable<void> {
          auto error = std::exception_ptr{};
          try {
             co_await std::move(operation);
          } catch (...) {
             error = std::current_exception();
          }

          {
             const auto lock = std::scoped_lock{shared->mutex};
             if (!shared->done) {
                shared->error = std::move(error);
                shared->done = true;
             }
          }
          timer->cancel();
          shared->ready.notify_all();
       },
       boost::asio::detached);

   boost::asio::co_spawn(
       runtime.context(),
       [shared, timer]() -> boost::asio::awaitable<void> {
          auto error = boost::system::error_code{};
          co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
          if (!error) {
             {
                const auto lock = std::scoped_lock{shared->mutex};
                if (!shared->done) {
                   shared->timed_out = true;
                   shared->done = true;
                }
             }
             shared->ready.notify_all();
          }
       },
       boost::asio::detached);

   auto lock = std::unique_lock{shared->mutex};
   shared->ready.wait(lock, [&] { return shared->done; });

   if (shared->timed_out) {
      return false;
   }
   if (shared->error) {
      std::rethrow_exception(shared->error);
   }
   return true;
}

} // namespace forge::asio::blocking
