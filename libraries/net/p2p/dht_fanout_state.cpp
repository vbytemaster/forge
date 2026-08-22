module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

module forge.net.p2p.node;

import forge.asio.notification;
import forge.net.p2p.identity;

#include "details/cancellation_latch.hxx"
#include "details/dht_fanout_state.hxx"
#include "details/lifecycle_wakeup.hxx"

namespace forge::net::p2p::detail {

dht_fanout_state::dht_fanout_state(std::size_t concurrency)
    : wakeup_{std::make_shared<lifecycle_wakeup>()} {
   completed_.reserve(concurrency);
}

void dht_fanout_state::publish(peer_id peer, std::shared_ptr<cancellation_latch> cancellation) {
   const auto [_, inserted] = active_.emplace(std::move(peer), std::move(cancellation));
   if (!inserted) {
      throw std::logic_error{"duplicate DHT fanout peer"};
   }
}

void dht_fanout_state::abandon(const peer_id& peer) noexcept {
   active_.erase(peer);
}

void dht_fanout_state::complete(const peer_id& peer, bool succeeded, std::exception_ptr error) noexcept {
   active_.erase(peer);
   try {
      completed_.push_back(completion{.succeeded = succeeded, .error = std::move(error)});
   } catch (...) {
      if (!completion_failure_) {
         completion_failure_ = std::current_exception();
      }
   }
   wakeup_->notify();
}

std::size_t dht_fanout_state::active_count() const noexcept {
   return active_.size();
}

void dht_fanout_state::request_stop() noexcept {
   stop_requested_.store(true, std::memory_order_release);
   wakeup_->notify();
}

bool dht_fanout_state::stop_requested() const noexcept {
   return stop_requested_.load(std::memory_order_acquire);
}

void dht_fanout_state::cancel_active() noexcept {
   for (const auto& [_, cancellation] : active_) {
      cancellation->request_stop();
   }
}

std::optional<dht_fanout_state::completion> dht_fanout_state::take_completion() noexcept {
   if (completion_failure_) {
      auto failure = std::exchange(completion_failure_, {});
      return completion{.error = std::move(failure)};
   }
   if (completed_.empty()) {
      return std::nullopt;
   }
   auto value = std::move(completed_.front());
   completed_.erase(completed_.begin());
   return value;
}

boost::asio::awaitable<std::optional<dht_fanout_state::completion>> dht_fanout_state::async_next_or_stop() {
   while (true) {
      if (stop_requested()) {
         co_return std::nullopt;
      }
      if (auto value = take_completion()) {
         co_return value;
      }
      const auto observed = wakeup_->epoch();
      if (stop_requested()) {
         co_return std::nullopt;
      }
      if (auto value = take_completion()) {
         co_return value;
      }
      static_cast<void>(co_await wakeup_->async_wait(observed));
   }
}

boost::asio::awaitable<std::optional<dht_fanout_state::completion>> dht_fanout_state::async_next_completion() {
   while (true) {
      if (auto value = take_completion()) {
         co_return value;
      }
      if (active_.empty()) {
         co_return std::nullopt;
      }
      const auto observed = wakeup_->epoch();
      if (auto value = take_completion()) {
         co_return value;
      }
      if (active_.empty()) {
         co_return std::nullopt;
      }
      static_cast<void>(co_await wakeup_->async_wait(observed));
   }
}

} // namespace forge::net::p2p::detail
