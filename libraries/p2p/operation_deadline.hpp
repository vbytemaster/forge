#pragma once

namespace fcl::p2p {

void validate_operation_timeout(std::chrono::milliseconds timeout, std::string_view name);

class operation_deadline {
 public:
   operation_deadline(boost::asio::io_context& context, std::chrono::milliseconds timeout);
   operation_deadline(const operation_deadline&) = delete;
   operation_deadline& operator=(const operation_deadline&) = delete;
   ~operation_deadline();

   void arm(std::function<void()> cancel);
   [[nodiscard]] bool finish() noexcept;
   void cancel() noexcept;
   [[nodiscard]] bool timed_out() const noexcept;

 private:
   enum class state_value : std::uint8_t { pending, completed, timed_out };

   std::shared_ptr<boost::asio::steady_timer> timer_;
   std::shared_ptr<std::atomic<state_value>> state_;
};

} // namespace fcl::p2p
