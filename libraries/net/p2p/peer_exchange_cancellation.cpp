module;

#include <cstddef>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

module forge.net.p2p.node;

#include "details/peer_exchange_cancellation.hxx"

namespace forge::net::p2p::detail {

void peer_exchange_cancellation::reserve(std::size_t capacity) {
   const auto lock = std::scoped_lock{mutex_};
   callbacks_.reserve(capacity);
}

void peer_exchange_cancellation::publish(std::function<void()> callback) noexcept {
   auto cancelled = std::vector<std::function<void()>>{};
   auto invoke_callback = false;
   try {
      const auto lock = std::scoped_lock{mutex_};
      if (cancel_requested_) {
         invoke_callback = true;
      } else {
         try {
            // Reserve is completed before worker publication. Default-construct
            // then swap so a failed vector growth leaves callback intact.
            callbacks_.emplace_back();
            callbacks_.back().swap(callback);
         } catch (...) {
            // A broken publication must fail closed rather than leave a worker
            // detached from the batch cancellation owner.
            cancel_requested_ = true;
            stop_requested_ = true;
            cancelled.swap(callbacks_);
            invoke_callback = true;
         }
      }
   } catch (...) {
      // Preserve the no-throw lifecycle boundary even for an unexpected mutex failure.
      invoke_callback = true;
   }
   if (invoke_callback) {
      invoke(std::move(callback));
   }
   invoke_all(cancelled);
}

void peer_exchange_cancellation::cancel() noexcept {
   auto callbacks = std::vector<std::function<void()>>{};
   try {
      const auto lock = std::scoped_lock{mutex_};
      if (cancel_requested_) {
         return;
      }
      cancel_requested_ = true;
      callbacks.swap(callbacks_);
   } catch (...) {
      return;
   }
   invoke_all(callbacks);
}

void peer_exchange_cancellation::request_stop() noexcept {
   auto callbacks = std::vector<std::function<void()>>{};
   try {
      const auto lock = std::scoped_lock{mutex_};
      if (stop_requested_) {
         return;
      }
      stop_requested_ = true;
      if (!cancel_requested_) {
         cancel_requested_ = true;
         callbacks.swap(callbacks_);
      }
   } catch (...) {
      return;
   }
   invoke_all(callbacks);
}

bool peer_exchange_cancellation::cancel_requested() const noexcept {
   try {
      const auto lock = std::scoped_lock{mutex_};
      return cancel_requested_;
   } catch (...) {
      return true;
   }
}

bool peer_exchange_cancellation::stop_requested() const noexcept {
   try {
      const auto lock = std::scoped_lock{mutex_};
      return stop_requested_;
   } catch (...) {
      return true;
   }
}

void peer_exchange_cancellation::invoke(std::function<void()>&& callback) noexcept {
   if (callback) {
      try {
         callback();
      } catch (...) {
         // Cancellation publication is best-effort at this no-throw boundary;
         // the operation owner remains responsible for terminal completion.
      }
   }
}

void peer_exchange_cancellation::invoke_all(std::vector<std::function<void()>>& callbacks) noexcept {
   for (auto& callback : callbacks) {
      invoke(std::move(callback));
   }
}

} // namespace forge::net::p2p::detail
