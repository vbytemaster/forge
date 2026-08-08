#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace forge::api::core::detail {

class stream_state final : public stream_endpoint {
 public:
   stream_state(std::size_t max_item_bytes,
                std::size_t max_buffered_items,
                std::size_t max_buffered_bytes);
   ~stream_state() override;

   boost::asio::awaitable<std::optional<bytes>> async_read() override;
   boost::asio::awaitable<void> async_write(bytes value) override;
   void close() noexcept override;
   void fail(std::exception_ptr error) noexcept override;
   void set_observer(
      std::function<void(stream_event, std::size_t)> observer) override;
   void set_failure_observer(std::function<void()> observer) override;

 private:
   using waiter = std::shared_ptr<boost::asio::steady_timer>;

   [[nodiscard]] bool has_capacity(std::size_t bytes) const noexcept;
   [[nodiscard]] std::exception_ptr terminal_error() const noexcept;
   static void wake(const waiter& value) noexcept;

   std::size_t max_item_bytes_ = 0;
   std::size_t max_buffered_items_ = 0;
   std::size_t max_buffered_bytes_ = 0;
   mutable std::mutex mutex_;
   std::deque<bytes> queue_;
   std::size_t buffered_bytes_ = 0;
   bool closed_ = false;
   std::exception_ptr error_;
   std::function<void(stream_event, std::size_t)> observer_;
   std::function<void()> failure_observer_;
   bool failure_observer_requested_ = false;
   bool failure_observer_notified_ = false;
   waiter read_waiter_;
   waiter write_waiter_;
};

} // namespace forge::api::core::detail
