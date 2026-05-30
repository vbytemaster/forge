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

module fcl.p2p.node;

import fcl.crypto.chacha20_poly1305;
import fcl.crypto.der;
import fcl.crypto.ed25519;
import fcl.crypto.hmac;
import fcl.crypto.pem;
import fcl.crypto.asymmetric;
import fcl.p2p.endpoint;
import fcl.p2p.envelope;
import fcl.p2p.hole_punch;
import fcl.p2p.identify;
import fcl.p2p.exceptions;
import fcl.p2p.message;
import fcl.p2p.negotiation;
import fcl.p2p.reachability;
import fcl.p2p.resource_manager;
import fcl.p2p.scoring;
import fcl.p2p.stream;
import fcl.crypto.random;
import fcl.crypto.rsa;
import fcl.crypto.sha256;
import fcl.crypto.x25519;
import fcl.multiformats.types;
import fcl.multiformats.varint;
import fcl.multiformats.exceptions;

#include "operation_deadline.hpp"

namespace fcl::p2p {

namespace asio = boost::asio;

operation_deadline::operation_deadline(boost::asio::io_context& context, std::chrono::milliseconds timeout)
    : timer_(std::make_shared<asio::steady_timer>(context)),
      state_(std::make_shared<std::atomic<state_value>>(state_value::pending)) {
   validate_operation_timeout(timeout, "P2P operation timeout");
   timer_->expires_after(timeout);
}

operation_deadline::~operation_deadline() {
   cancel();
}

void operation_deadline::arm(std::function<void()> cancel) {
   auto timer = timer_;
   auto state = state_;
   timer_->async_wait([timer, state, cancel = std::move(cancel)](boost::system::error_code ec) mutable {
      if (ec) {
         return;
      }
      auto expected = state_value::pending;
      if (!state->compare_exchange_strong(expected, state_value::timed_out, std::memory_order_acq_rel)) {
         return;
      }
      cancel();
   });
}

[[nodiscard]] bool operation_deadline::finish() noexcept {
   auto expected = state_value::pending;
   if (state_->compare_exchange_strong(expected, state_value::completed, std::memory_order_acq_rel)) {
      cancel();
      return true;
   }
   cancel();
   return state_->load(std::memory_order_acquire) != state_value::timed_out;
}

void operation_deadline::cancel() noexcept {
   if (!timer_) {
      return;
   }
   try {
      timer_->cancel();
   } catch (...) {
      // Timer cancellation must not escape destructor/cleanup paths.
   }
}

[[nodiscard]] bool operation_deadline::timed_out() const noexcept {
   return state_->load(std::memory_order_acquire) == state_value::timed_out;
}

} // namespace fcl::p2p
