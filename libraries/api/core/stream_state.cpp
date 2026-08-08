module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

module forge.api.core.stream_reader;

#include "details/stream_state.hxx"

namespace forge::api::core::detail {

stream_state::stream_state(std::size_t max_item_bytes,
                           std::size_t max_buffered_items,
                           std::size_t max_buffered_bytes)
    : max_item_bytes_{max_item_bytes},
      max_buffered_items_{max_buffered_items},
      max_buffered_bytes_{max_buffered_bytes} {
   if (max_item_bytes_ == 0 || max_buffered_items_ == 0 ||
       max_buffered_bytes_ == 0 || max_item_bytes_ > max_buffered_bytes_) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "invalid API stream limits");
   }
}

stream_state::~stream_state() {
   if (observer_ && buffered_bytes_ != 0) {
      try {
         observer_(stream_event::dropped, buffered_bytes_);
      } catch (...) {
         // Destruction cannot report observer failures.
      }
   }
}

bool stream_state::has_capacity(std::size_t bytes_value) const noexcept {
   return queue_.size() < max_buffered_items_ &&
          bytes_value <= max_buffered_bytes_ - buffered_bytes_;
}

std::exception_ptr stream_state::terminal_error() const noexcept {
   return error_;
}

void stream_state::wake(const waiter& value) noexcept {
   if (!value) {
      return;
   }
   boost::asio::dispatch(value->get_executor(), [value] {
      try {
         value->expires_at(boost::asio::steady_timer::time_point::min());
         value->cancel();
      } catch (...) {
         // Stream completion and cancellation paths must remain noexcept.
      }
   });
}

boost::asio::awaitable<std::optional<bytes>> stream_state::async_read() {
   const auto executor = co_await boost::asio::this_coro::executor;
   while (true) {
      auto pending = waiter{};
      auto ready_writer = waiter{};
      auto item = std::optional<bytes>{};
      auto error = std::exception_ptr{};
      auto observer =
         std::function<void(stream_event, std::size_t)>{};
      auto consumed_bytes = std::size_t{0};
      auto ended = false;
      {
         const auto lock = std::scoped_lock{mutex_};
         error = terminal_error();
         if (!error && !queue_.empty()) {
            buffered_bytes_ -= queue_.front().size();
            consumed_bytes = queue_.front().size();
            item.emplace(std::move(queue_.front()));
            queue_.pop_front();
            ready_writer = write_waiter_;
            observer = observer_;
         } else if (!error && closed_) {
            ended = true;
         } else if (!error) {
            if (read_waiter_) {
               FORGE_THROW_EXCEPTION(exceptions::protocol_error, "API stream already has a pending read");
            }
            pending = std::make_shared<boost::asio::steady_timer>(
               executor, boost::asio::steady_timer::time_point::max());
            read_waiter_ = pending;
         }
      }

      wake(ready_writer);
      if (error) {
         std::rethrow_exception(error);
      }
      if (item) {
         if (observer) {
            try {
               observer(stream_event::consumed, consumed_bytes);
            } catch (...) {
               // Accounting observers are internal no-throw callbacks. A
               // faulty observer must not make a delivered item disappear.
            }
         }
         co_return std::move(item);
      }
      if (ended) {
         co_return std::nullopt;
      }

      auto wait_error = boost::system::error_code{};
      co_await pending->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, wait_error));
      const auto cancellation =
         co_await boost::asio::this_coro::cancellation_state;
      {
         const auto lock = std::scoped_lock{mutex_};
         if (read_waiter_ == pending) {
            read_waiter_.reset();
         }
      }
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
         FORGE_THROW_EXCEPTION(exceptions::cancelled,
                               "API stream read was cancelled");
      }
   }
}

boost::asio::awaitable<void> stream_state::async_write(bytes value) {
   if (value.size() > max_item_bytes_ || value.size() > max_buffered_bytes_) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "API stream item exceeds configured limit",
                            forge::exceptions::ctx("item_bytes", value.size()),
                            forge::exceptions::ctx("max_item_bytes", max_item_bytes_));
   }

   const auto executor = co_await boost::asio::this_coro::executor;
   while (true) {
      auto pending = waiter{};
      auto ready_reader = waiter{};
      auto error = std::exception_ptr{};
      auto wrote = false;
      auto closed = false;
      {
         const auto lock = std::scoped_lock{mutex_};
         error = terminal_error();
         closed = closed_;
         if (!error && !closed && has_capacity(value.size())) {
            buffered_bytes_ += value.size();
            queue_.push_back(std::move(value));
            ready_reader = read_waiter_;
            wrote = true;
         } else if (!error && !closed) {
            if (write_waiter_) {
               FORGE_THROW_EXCEPTION(exceptions::protocol_error, "API stream already has a pending write");
            }
            pending = std::make_shared<boost::asio::steady_timer>(
               executor, boost::asio::steady_timer::time_point::max());
            write_waiter_ = pending;
         }
      }

      wake(ready_reader);
      if (error) {
         std::rethrow_exception(error);
      }
      if (closed) {
         FORGE_THROW_EXCEPTION(exceptions::cancelled, "API stream writer is closed");
      }
      if (wrote) {
         co_return;
      }

      auto wait_error = boost::system::error_code{};
      co_await pending->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, wait_error));
      const auto cancellation =
         co_await boost::asio::this_coro::cancellation_state;
      {
         const auto lock = std::scoped_lock{mutex_};
         if (write_waiter_ == pending) {
            write_waiter_.reset();
         }
      }
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
         FORGE_THROW_EXCEPTION(exceptions::cancelled,
                               "API stream write was cancelled");
      }
   }
}

void stream_state::close() noexcept {
   auto reader = waiter{};
   auto writer = waiter{};
   {
      const auto lock = std::scoped_lock{mutex_};
      if (closed_) {
         return;
      }
      closed_ = true;
      reader = read_waiter_;
      writer = write_waiter_;
   }
   wake(reader);
   wake(writer);
}

void stream_state::fail(std::exception_ptr error) noexcept {
   if (!error) {
      return;
   }
   auto reader = waiter{};
   auto writer = waiter{};
   auto observer = std::function<void(stream_event, std::size_t)>{};
   auto failure_observer = std::function<void()>{};
   auto dropped_bytes = std::size_t{0};
   auto stream_closed = false;
   {
      const auto lock = std::scoped_lock{mutex_};
      if (error_) {
         return;
      }
      failure_observer_requested_ = true;
      if (failure_observer_ && !failure_observer_notified_) {
         failure_observer_notified_ = true;
         failure_observer = failure_observer_;
      }
      stream_closed = closed_;
      if (!stream_closed) {
         error_ = std::move(error);
         closed_ = true;
         dropped_bytes = buffered_bytes_;
         queue_.clear();
         buffered_bytes_ = 0;
         observer = observer_;
         reader = read_waiter_;
         writer = write_waiter_;
      }
   }
   if (stream_closed) {
      if (failure_observer) {
         try {
            failure_observer();
         } catch (...) {
            // Failure paths must remain noexcept.
         }
      }
      return;
   }
   wake(reader);
   wake(writer);
   if (observer && dropped_bytes != 0) {
      try {
         observer(stream_event::dropped, dropped_bytes);
      } catch (...) {
         // Failure paths must remain noexcept.
      }
   }
   if (failure_observer) {
      try {
         failure_observer();
      } catch (...) {
         // Failure paths must remain noexcept.
      }
   }
}

void stream_state::set_observer(
   std::function<void(stream_event, std::size_t)> observer) {
   const auto lock = std::scoped_lock{mutex_};
   observer_ = std::move(observer);
}

void stream_state::set_failure_observer(std::function<void()> observer) {
   auto notify = std::function<void()>{};
   {
      const auto lock = std::scoped_lock{mutex_};
      failure_observer_ = std::move(observer);
      if (failure_observer_requested_ && failure_observer_ &&
          !failure_observer_notified_) {
         failure_observer_notified_ = true;
         notify = failure_observer_;
      }
   }
   if (notify) {
      notify();
   }
}

} // namespace forge::api::core::detail
