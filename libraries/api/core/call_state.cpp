module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

module forge.api.core.call_options;

#include "details/call_state.hxx"

namespace forge::api::core::detail {

call_state::call_state(boost::asio::any_io_executor executor,
                       std::vector<std::shared_ptr<stream_endpoint>> endpoints)
    : call_state{std::move(executor), std::move(endpoints), std::nullopt} {}

call_state::call_state(
   boost::asio::any_io_executor executor,
   std::vector<std::shared_ptr<stream_endpoint>> endpoints,
   std::optional<std::chrono::steady_clock::time_point> deadline)
    : executor_(std::move(executor)), endpoints_(std::move(endpoints)),
      deadline_(deadline) {}

void call_state::wake(const std::shared_ptr<boost::asio::steady_timer>& value) noexcept {
   if (!value) {
      return;
   }
   boost::asio::dispatch(value->get_executor(), [value] {
      try {
         value->expires_at(boost::asio::steady_timer::time_point::min());
         value->cancel();
      } catch (...) {
         // Call completion and cancellation paths must remain noexcept.
      }
   });
}

void call_state::start(boost::asio::awaitable<call_result> body) {
   {
      const auto lock = std::scoped_lock{mutex_};
      if (started_) {
         throw exceptions::protocol_error{"API call operation already started"};
      }
      started_ = true;
      if (deadline_) {
         deadline_timer_ = std::make_shared<boost::asio::steady_timer>(
            executor_, *deadline_);
      }
   }

   auto self = shared_from_this();
   if (deadline_timer_) {
      deadline_timer_->async_wait([self](const boost::system::error_code& error) {
         if (!error) {
            self->expire();
         }
      });
   }
   boost::asio::co_spawn(
       executor_, std::move(body),
       boost::asio::bind_cancellation_slot(
           cancellation_.slot(), [self](std::exception_ptr error, call_result result) mutable {
              self->complete(std::move(error), std::move(result));
           }));
}

void call_state::cancel() noexcept {
   cancel_with(std::make_exception_ptr(
      exceptions::cancelled{"API stream call cancelled"}));
}

void call_state::expire() noexcept {
   cancel_with(std::make_exception_ptr(
      exceptions::deadline_exceeded{"API stream call deadline exceeded"}));
}

void call_state::cancel_with(std::exception_ptr error) noexcept {
   auto should_cancel = false;
   auto waiter = std::shared_ptr<boost::asio::steady_timer>{};
   auto deadline_timer = std::shared_ptr<boost::asio::steady_timer>{};
   {
      const auto lock = std::scoped_lock{mutex_};
      if (!completed_) {
         completed_ = true;
         error_ = error;
         waiter = finish_waiter_;
         deadline_timer = deadline_timer_;
         should_cancel = true;
      }
   }
   if (!should_cancel) {
      return;
   }

   for (const auto& endpoint : endpoints_) {
      endpoint->fail(error);
   }

   auto self = shared_from_this();
   boost::asio::dispatch(executor_, [self] {
      self->cancellation_.emit(boost::asio::cancellation_type::all);
   });
   wake(deadline_timer);
   wake(waiter);
}

void call_state::complete(std::exception_ptr error, call_result result) noexcept {
   auto waiter = std::shared_ptr<boost::asio::steady_timer>{};
   auto deadline_timer = std::shared_ptr<boost::asio::steady_timer>{};
   auto terminal_error = std::exception_ptr{};
   {
      const auto lock = std::scoped_lock{mutex_};
      if (completed_) {
         return;
      }
      completed_ = true;
      error_ = std::move(error);
      result_ = std::move(result);
      terminal_error = error_;
      waiter = finish_waiter_;
      deadline_timer = deadline_timer_;
   }

   if (terminal_error) {
      for (const auto& endpoint : endpoints_) {
         endpoint->fail(terminal_error);
      }
   } else {
      for (const auto& endpoint : endpoints_) {
         endpoint->close();
      }
   }
   wake(deadline_timer);
   wake(waiter);
}

boost::asio::awaitable<call_result> call_state::async_finish() {
   const auto executor = co_await boost::asio::this_coro::executor;
   while (true) {
      auto waiter = std::shared_ptr<boost::asio::steady_timer>{};
      auto error = std::exception_ptr{};
      auto result = call_result{};
      {
         const auto lock = std::scoped_lock{mutex_};
         if (finish_consumed_ || finish_pending_) {
            throw exceptions::protocol_error{
               "API stream call finish already awaited"};
         }
         if (completed_) {
            finish_consumed_ = true;
            error = error_;
            result = std::move(result_);
         } else {
            finish_pending_ = true;
            waiter = std::make_shared<boost::asio::steady_timer>(
               executor, boost::asio::steady_timer::time_point::max());
            finish_waiter_ = waiter;
         }
      }

      if (!waiter) {
         if (error) {
            std::rethrow_exception(error);
         }
         co_return result;
      }

      auto wait_error = boost::system::error_code{};
      co_await waiter->async_wait(
         boost::asio::redirect_error(boost::asio::use_awaitable, wait_error));
      const auto cancellation =
         co_await boost::asio::this_coro::cancellation_state;
      {
         const auto lock = std::scoped_lock{mutex_};
         finish_pending_ = false;
         if (finish_waiter_ == waiter) {
            finish_waiter_.reset();
         }
      }
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
         throw exceptions::cancelled{"API stream call finish was cancelled"};
      }
   }
}

} // namespace forge::api::core::detail
