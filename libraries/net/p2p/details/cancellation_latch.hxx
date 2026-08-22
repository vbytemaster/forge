#pragma once

namespace forge::net::p2p {

class cancellation_latch {
 private:
   struct observer;

 public:
   class subscription {
    public:
      subscription() = default;
      subscription(const subscription&) = delete;
      subscription& operator=(const subscription&) = delete;
      subscription(subscription&& other) noexcept;
      subscription& operator=(subscription&& other) noexcept;
      ~subscription();

      void reset() noexcept;

    private:
      friend class cancellation_latch;

      subscription(std::shared_ptr<cancellation_latch> parent, std::shared_ptr<observer> observer);

      std::shared_ptr<cancellation_latch> parent_;
      std::shared_ptr<observer> observer_;
   };

   void arm(std::function<void()> cancel);
   [[nodiscard]] static subscription subscribe(const std::shared_ptr<cancellation_latch>& parent,
                                               std::function<void()> cancel);
   void request_stop() noexcept;
   [[nodiscard]] bool stop_requested() const noexcept;
   void clear() noexcept;
   [[nodiscard]] bool finish() noexcept;

 private:
   enum class state { open, stop_requested, completed, stopped };

   struct observer {
      std::function<void()> cancel;
   };

   void complete_callback() noexcept;
   void unsubscribe(const std::shared_ptr<observer>& observer) noexcept;

   mutable std::mutex mutex_;
   std::condition_variable completion_;
   std::function<void()> cancel_;
   std::vector<std::shared_ptr<observer>> observers_;
   state state_ = state::open;
   unsigned active_callbacks_ = 0;
};

} // namespace forge::net::p2p
