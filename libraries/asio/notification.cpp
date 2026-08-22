module;

#include "details/notification_waiter.hxx"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

module forge.asio.notification;

#include "details/notification_impl.hxx"

namespace forge::asio {

notification::notification() : impl_(std::make_shared<impl>()) {}

notification::~notification() = default;
notification::notification(notification&&) noexcept = default;
notification& notification::operator=(notification&&) noexcept = default;

notification::epoch_type notification::epoch() const noexcept {
   return impl_->epoch();
}

boost::asio::awaitable<notification::epoch_type> notification::async_wait(epoch_type observed_epoch) {
   auto state = impl_;
   co_return co_await state->async_wait(observed_epoch);
}

boost::asio::awaitable<notification::epoch_type>
notification::async_wait(epoch_type observed_epoch, std::stop_token stop) {
   auto state = impl_;
   co_return co_await state->async_wait(observed_epoch, std::move(stop));
}

boost::asio::awaitable<notification::epoch_type>
notification::async_wait_until(epoch_type observed_epoch, std::chrono::steady_clock::time_point deadline) {
   auto state = impl_;
   co_return co_await state->async_wait_until(observed_epoch, deadline);
}

boost::asio::awaitable<notification::epoch_type>
notification::async_wait_until(epoch_type observed_epoch, std::chrono::steady_clock::time_point deadline,
                               std::stop_token stop) {
   auto state = impl_;
   co_return co_await state->async_wait_until(observed_epoch, deadline, std::move(stop));
}

void notification::notify() noexcept {
   impl_->notify();
}

} // namespace forge::asio
