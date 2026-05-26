#pragma once

namespace fcl::p2p {

class operation_deadline {
 public:
   operation_deadline(boost::asio::io_context& context, std::chrono::milliseconds timeout)
       : timer_(std::make_shared<asio::steady_timer>(context)),
         state_(std::make_shared<std::atomic<state_value>>(state_value::pending)) {
      validate_operation_timeout(timeout, "P2P operation timeout");
      timer_->expires_after(timeout);
   }

   operation_deadline(const operation_deadline&) = delete;
   operation_deadline& operator=(const operation_deadline&) = delete;

   ~operation_deadline() {
      cancel();
   }

   template <typename Cancel> void arm(Cancel cancel) {
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

   [[nodiscard]] bool finish() noexcept {
      auto expected = state_value::pending;
      if (state_->compare_exchange_strong(expected, state_value::completed, std::memory_order_acq_rel)) {
         cancel();
         return true;
      }
      cancel();
      return state_->load(std::memory_order_acquire) != state_value::timed_out;
   }

   void cancel() noexcept {
      if (!timer_) {
         return;
      }
      try {
         timer_->cancel();
      } catch (...) {
         // Timer cancellation must not escape destructor/cleanup paths.
      }
   }

   [[nodiscard]] bool timed_out() const noexcept {
      return state_->load(std::memory_order_acquire) == state_value::timed_out;
   }

 private:
   enum class state_value : std::uint8_t { pending, completed, timed_out };

   std::shared_ptr<asio::steady_timer> timer_;
   std::shared_ptr<std::atomic<state_value>> state_;
};

} // namespace fcl::p2p
