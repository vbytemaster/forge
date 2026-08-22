#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

namespace forge::asio::detail {

class notification_waiter;

class async_waiter : public std::enable_shared_from_this<async_waiter> {
 public:
   explicit async_waiter(boost::asio::any_io_executor executor);

   boost::asio::awaitable<boost::system::error_code> wait();
   boost::asio::awaitable<boost::system::error_code> wait_until(
       std::chrono::steady_clock::time_point deadline);
   void wake() noexcept;

 private:
   boost::asio::awaitable<boost::system::error_code> wait_until_impl(
       std::chrono::steady_clock::time_point deadline);
   void unsubscribe(const std::shared_ptr<notification_waiter>& waiter) noexcept;

   boost::asio::any_io_executor executor_;
   std::mutex mutex_;
   std::vector<std::weak_ptr<notification_waiter>> waiters_;
   bool wake_requested_ = false;
};

} // namespace forge::asio::detail
