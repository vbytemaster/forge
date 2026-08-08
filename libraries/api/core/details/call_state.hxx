#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace forge::api::core::detail {

class call_state final : public call_operation, public std::enable_shared_from_this<call_state> {
 public:
   call_state(boost::asio::any_io_executor executor, std::vector<std::shared_ptr<stream_endpoint>> endpoints);
   call_state(boost::asio::any_io_executor executor,
              std::vector<std::shared_ptr<stream_endpoint>> endpoints,
              std::optional<std::chrono::steady_clock::time_point> deadline);

   void start(boost::asio::awaitable<call_result> body) override;
   void cancel() noexcept override;
   boost::asio::awaitable<call_result> async_finish() override;

 private:
   void cancel_with(std::exception_ptr error) noexcept;
   void complete(std::exception_ptr error, call_result result) noexcept;
   void expire() noexcept;
   static void wake(const std::shared_ptr<boost::asio::steady_timer>& value) noexcept;

   boost::asio::any_io_executor executor_;
   boost::asio::cancellation_signal cancellation_;
   std::vector<std::shared_ptr<stream_endpoint>> endpoints_;
   std::optional<std::chrono::steady_clock::time_point> deadline_;
   std::mutex mutex_;
   bool started_ = false;
   bool completed_ = false;
   bool finish_pending_ = false;
   bool finish_consumed_ = false;
   std::exception_ptr error_;
   call_result result_;
   std::shared_ptr<boost::asio::steady_timer> deadline_timer_;
   std::shared_ptr<boost::asio::steady_timer> finish_waiter_;
};

} // namespace forge::api::core::detail
