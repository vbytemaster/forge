#include "details/async_waiter.hxx"
#include "details/notification_waiter.hxx"

#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace forge::asio::detail {

async_waiter::async_waiter(boost::asio::any_io_executor executor)
    : executor_{std::move(executor)} {}

boost::asio::awaitable<boost::system::error_code> async_waiter::wait() {
   co_return co_await wait_until_impl(boost::asio::steady_timer::time_point::max());
}

boost::asio::awaitable<boost::system::error_code>
async_waiter::wait_until(std::chrono::steady_clock::time_point deadline) {
   co_return co_await wait_until_impl(deadline);
}

boost::asio::awaitable<boost::system::error_code>
async_waiter::wait_until_impl(std::chrono::steady_clock::time_point deadline) {
   auto waiter = std::make_shared<notification_waiter>(executor_);
   auto complete_immediately = false;
   {
      const auto lock = std::scoped_lock{mutex_};
      if (wake_requested_) {
         complete_immediately = true;
      } else {
         std::erase_if(waiters_, [](const auto& candidate) { return candidate.expired(); });
         waiters_.emplace_back(waiter);
      }
   }
   if (complete_immediately) {
      waiter->wake();
   }

   auto error = boost::system::error_code{};
   try {
      error = co_await waiter->wait_until(deadline);
   } catch (...) {
      unsubscribe(waiter);
      throw;
   }
   unsubscribe(waiter);
   co_return error;
}

void async_waiter::unsubscribe(const std::shared_ptr<notification_waiter>& waiter) noexcept {
   const auto lock = std::scoped_lock{mutex_};
   std::erase_if(waiters_, [&waiter](const auto& candidate) {
      const auto pending = candidate.lock();
      return !pending || pending == waiter;
   });
}

void async_waiter::wake() noexcept {
   auto waiters = std::vector<std::weak_ptr<notification_waiter>>{};
   {
      const auto lock = std::scoped_lock{mutex_};
      wake_requested_ = true;
      waiters.swap(waiters_);
   }
   for (const auto& candidate : waiters) {
      if (const auto waiter = candidate.lock()) {
         waiter->wake();
      }
   }
}

} // namespace forge::asio::detail
