#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <vector>

namespace forge::net::p2p::detail {

class peer_exchange_cancellation {
 public:
   void reserve(std::size_t capacity);
   void publish(std::function<void()> callback) noexcept;
   void cancel() noexcept;
   void request_stop() noexcept;
   [[nodiscard]] bool cancel_requested() const noexcept;
   [[nodiscard]] bool stop_requested() const noexcept;

 private:
   static void invoke(std::function<void()>&& callback) noexcept;
   static void invoke_all(std::vector<std::function<void()>>& callbacks) noexcept;

   mutable std::mutex mutex_;
   std::vector<std::function<void()>> callbacks_;
   bool cancel_requested_ = false;
   bool stop_requested_ = false;
};

} // namespace forge::net::p2p::detail
