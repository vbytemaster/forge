module;

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

module forge.net.p2p.node;

import forge.crypto.symmetric.chacha20_poly1305;
import forge.crypto.pki.der;
import forge.crypto.asymmetric.ed25519;
import forge.crypto.digest.hmac;
import forge.crypto.pki.pem;
import forge.crypto.asymmetric;
import forge.net.p2p.endpoint;
import forge.net.p2p.envelope;
import forge.net.p2p.hole_punch;
import forge.net.p2p.identify;
import forge.net.p2p.exceptions;
import forge.net.p2p.message;
import forge.net.p2p.negotiation;
import forge.net.p2p.reachability;
import forge.net.p2p.resource_manager;
import forge.net.p2p.scoring;
import forge.net.p2p.stream;
import forge.crypto.core.random;
import forge.crypto.asymmetric.rsa;
import forge.crypto.digest.sha256;
import forge.crypto.asymmetric.x25519;
import forge.multiformats.types;
import forge.multiformats.varint;
import forge.multiformats.exceptions;

#include "details/operation_deadline.hxx"

namespace forge::net::p2p {

namespace asio = boost::asio;

namespace {

thread_local const void* executing_cancel_state = nullptr;

} // namespace

operation_deadline::stop_token::stop_token(std::shared_ptr<shared_state> state) : state_{std::move(state)} {}

[[nodiscard]] bool operation_deadline::stop_token::request_stop() const noexcept {
   if (!state_) {
      return false;
   }
   auto claim = callback_claim{};
   {
      auto lock = std::scoped_lock{state_->mutex};
      if (state_->finished || state_->value != state_value::pending) {
         return false;
      }
      state_->value = state_value::stopped;
      claim = operation_deadline::claim_cancel_locked(state_);
   }
   operation_deadline::invoke_cancel(std::move(claim));
   return true;
}

operation_deadline::operation_deadline(boost::asio::io_context& context, std::chrono::milliseconds timeout)
    : timer_(std::make_shared<asio::steady_timer>(context)), state_(std::make_shared<shared_state>()) {
   validate_operation_timeout(timeout, "P2P operation timeout");
   timer_->expires_after(timeout);
   auto timer = timer_;
   auto state = state_;
   timer_->async_wait([timer = std::move(timer), state = std::move(state)](boost::system::error_code ec) {
      if (ec) {
         return;
      }
      auto claim = callback_claim{};
      {
         auto lock = std::scoped_lock{state->mutex};
         if (state->finished || state->value != state_value::pending) {
            return;
         }
         state->value = state_value::timed_out;
         claim = operation_deadline::claim_cancel_locked(state);
      }
      operation_deadline::invoke_cancel(std::move(claim));
   });
}

operation_deadline::~operation_deadline() {
   static_cast<void>(finish());
}

void operation_deadline::arm(std::function<void()> cancel) {
   auto claim = callback_claim{};
   {
      auto lock = std::scoped_lock{state_->mutex};
      if (state_->finished) {
         return;
      }
      state_->cancel = std::move(cancel);
      claim = claim_cancel_locked(state_);
   }
   invoke_cancel(std::move(claim));
}

operation_deadline::callback_claim
operation_deadline::claim_cancel_locked(const std::shared_ptr<shared_state>& state) {
   if (state->finished || state->cancel_invoked || !state->cancel ||
       (state->value != state_value::stopped && state->value != state_value::timed_out)) {
      return {};
   }
   state->cancel_invoked = true;
   state->active_callbacks.fetch_add(1, std::memory_order_relaxed);
   return callback_claim{.state = state, .callback = std::move(state->cancel)};
}

void operation_deadline::invoke_cancel(callback_claim claim) noexcept {
   if (!claim.callback) {
      return;
   }
   const auto* previous = executing_cancel_state;
   executing_cancel_state = claim.state.get();
   try {
      claim.callback();
   } catch (...) {
      // Deadline delivery must not escape an Asio handler or stop path.
      // Supported callbacks publish sticky per-operation cancellation.
   }
   executing_cancel_state = previous;
   claim.state->active_callbacks.fetch_sub(1, std::memory_order_release);
   claim.state->active_callbacks.notify_all();
}

[[nodiscard]] bool operation_deadline::finish() noexcept {
   auto result = true;
   {
      auto lock = std::scoped_lock{state_->mutex};
      state_->finished = true;
      if (state_->value == state_value::pending) {
         state_->value = state_value::completed;
      }
      state_->cancel = {};
      result = state_->value != state_value::timed_out;
   }
   try {
      timer_->cancel();
   } catch (...) {
      // Timer cancellation must not escape destructor/cleanup paths.
   }
   if (executing_cancel_state != state_.get()) {
      auto active = state_->active_callbacks.load(std::memory_order_acquire);
      while (active != 0) {
         state_->active_callbacks.wait(active, std::memory_order_acquire);
         active = state_->active_callbacks.load(std::memory_order_acquire);
      }
   }
   return result;
}

void operation_deadline::cancel() noexcept {
   static_cast<void>(finish());
}

[[nodiscard]] operation_deadline::stop_token operation_deadline::stopping() const noexcept {
   return stop_token{state_};
}

[[nodiscard]] bool operation_deadline::timed_out() const noexcept {
   auto lock = std::scoped_lock{state_->mutex};
   return state_->value == state_value::timed_out;
}

[[nodiscard]] bool operation_deadline::stopped() const noexcept {
   auto lock = std::scoped_lock{state_->mutex};
   return state_->value == state_value::stopped;
}

} // namespace forge::net::p2p
