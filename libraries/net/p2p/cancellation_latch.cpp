module;

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

module forge.net.p2p.node;

#include "details/cancellation_latch.hxx"

namespace forge::net::p2p {

cancellation_latch::subscription::subscription(std::shared_ptr<cancellation_latch> parent,
                                               std::shared_ptr<cancellation_latch::observer> observer)
    : parent_{std::move(parent)}, observer_{std::move(observer)} {}

cancellation_latch::subscription::subscription(subscription&& other) noexcept
    : parent_{std::move(other.parent_)}, observer_{std::move(other.observer_)} {}

cancellation_latch::subscription& cancellation_latch::subscription::operator=(subscription&& other) noexcept {
   if (this != &other) {
      reset();
      parent_ = std::move(other.parent_);
      observer_ = std::move(other.observer_);
   }
   return *this;
}

cancellation_latch::subscription::~subscription() {
   reset();
}

void cancellation_latch::subscription::reset() noexcept {
   if (parent_ && observer_) {
      parent_->unsubscribe(observer_);
   }
   parent_.reset();
   observer_.reset();
}

void cancellation_latch::arm(std::function<void()> cancel) {
   auto invoke = std::function<void()>{};
   {
      auto lock = std::scoped_lock{mutex_};
      if (state_ == state::completed || state_ == state::stopped) {
         return;
      }
      if (state_ == state::open) {
         cancel_ = std::move(cancel);
         return;
      }
      ++active_callbacks_;
      invoke = std::move(cancel);
   }
   try {
      if (invoke) {
         invoke();
      }
   } catch (...) {
      complete_callback();
      throw;
   }
   complete_callback();
}

cancellation_latch::subscription cancellation_latch::subscribe(const std::shared_ptr<cancellation_latch>& parent,
                                                               std::function<void()> cancel) {
   if (!parent || !cancel) {
      return {};
   }

   auto invoke = std::function<void()>{};
   auto value = std::shared_ptr<cancellation_latch::observer>{};
   {
      const auto lock = std::scoped_lock{parent->mutex_};
      if (parent->state_ == state::open) {
         value = std::make_shared<cancellation_latch::observer>();
         value->cancel = std::move(cancel);
         parent->observers_.push_back(value);
      } else if (parent->state_ == state::stop_requested || parent->state_ == state::stopped) {
         ++parent->active_callbacks_;
         invoke = std::move(cancel);
      }
   }

   if (invoke) {
      try {
         invoke();
      } catch (...) {
         parent->complete_callback();
         throw;
      }
      parent->complete_callback();
      return {};
   }
   if (!value) {
      return {};
   }
   return subscription{parent, std::move(value)};
}

void cancellation_latch::request_stop() noexcept {
   auto cancel = std::function<void()>{};
   auto observers = std::vector<std::shared_ptr<observer>>{};
   {
      auto lock = std::scoped_lock{mutex_};
      if (state_ != state::open) {
         return;
      }
      state_ = state::stop_requested;
      cancel = std::move(cancel_);
      observers.swap(observers_);
      auto callbacks = static_cast<unsigned>(static_cast<bool>(cancel));
      for (const auto& observer : observers) {
         if (observer && observer->cancel) {
            ++callbacks;
         }
      }
      active_callbacks_ += callbacks;
   }
   const auto invoke = [this](std::function<void()> callback) {
      try {
         callback();
      } catch (...) {
         // request_stop() is a no-throw cross-thread boundary. Production
         // callbacks publish sticky operation cancellation; containment here
         // preserves callback accounting for defensive/custom callbacks.
      }
      complete_callback();
   };
   if (cancel) {
      invoke(std::move(cancel));
   }
   for (const auto& observer : observers) {
      if (observer && observer->cancel) {
         invoke(std::move(observer->cancel));
      }
   }
}

bool cancellation_latch::stop_requested() const noexcept {
   const auto lock = std::scoped_lock{mutex_};
   return state_ == state::stop_requested || state_ == state::stopped;
}

void cancellation_latch::clear() noexcept {
   auto lock = std::unique_lock{mutex_};
   completion_.wait(lock, [this] { return active_callbacks_ == 0; });
   cancel_ = {};
   observers_.clear();
}

[[nodiscard]] bool cancellation_latch::finish() noexcept {
   auto lock = std::unique_lock{mutex_};
   if (state_ == state::open) {
      state_ = state::completed;
   } else if (state_ == state::stop_requested) {
      state_ = state::stopped;
   }
   cancel_ = {};
   observers_.clear();
   completion_.wait(lock, [this] { return active_callbacks_ == 0; });
   return state_ == state::completed;
}

void cancellation_latch::unsubscribe(const std::shared_ptr<observer>& observer) noexcept {
   try {
      const auto lock = std::scoped_lock{mutex_};
      std::erase(observers_, observer);
   } catch (...) {
   }
}

void cancellation_latch::complete_callback() noexcept {
   {
      auto lock = std::scoped_lock{mutex_};
      if (active_callbacks_ != 0) {
         --active_callbacks_;
      }
   }
   completion_.notify_all();
}

} // namespace forge::net::p2p
