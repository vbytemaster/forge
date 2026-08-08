#pragma once

#include <atomic>
#include <cstdint>

namespace forge::net::stcp::detail {

enum class handshake_terminal_state : std::uint8_t {
   pending,
   completed,
   timed_out,
};

class handshake_deadline_state {
 public:
   [[nodiscard]] bool try_complete() noexcept {
      auto expected = handshake_terminal_state::pending;
      return state_.compare_exchange_strong(expected, handshake_terminal_state::completed, std::memory_order_acq_rel);
   }

   [[nodiscard]] bool try_timeout() noexcept {
      auto expected = handshake_terminal_state::pending;
      return state_.compare_exchange_strong(expected, handshake_terminal_state::timed_out, std::memory_order_acq_rel);
   }

   [[nodiscard]] handshake_terminal_state current() const noexcept {
      return state_.load(std::memory_order_acquire);
   }

 private:
   std::atomic<handshake_terminal_state> state_{handshake_terminal_state::pending};
};

} // namespace forge::net::stcp::detail
