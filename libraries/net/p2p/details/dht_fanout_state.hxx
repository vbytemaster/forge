#pragma once

namespace forge::net::p2p {

class cancellation_latch;

namespace detail {

class lifecycle_wakeup;

class dht_fanout_state final {
 public:
   struct completion {
      bool succeeded = false;
      std::exception_ptr error;
   };

   explicit dht_fanout_state(std::size_t concurrency);

   void publish(peer_id peer, std::shared_ptr<cancellation_latch> cancellation);
   void abandon(const peer_id& peer) noexcept;
   void complete(const peer_id& peer, bool succeeded, std::exception_ptr error) noexcept;

   [[nodiscard]] std::size_t active_count() const noexcept;
   void request_stop() noexcept;
   [[nodiscard]] bool stop_requested() const noexcept;
   void cancel_active() noexcept;

   boost::asio::awaitable<std::optional<completion>> async_next_or_stop();
   boost::asio::awaitable<std::optional<completion>> async_next_completion();

 private:
   [[nodiscard]] std::optional<completion> take_completion() noexcept;

   std::map<peer_id, std::shared_ptr<cancellation_latch>> active_;
   std::vector<completion> completed_;
   std::shared_ptr<lifecycle_wakeup> wakeup_;
   std::exception_ptr completion_failure_;
   std::atomic_bool stop_requested_ = false;
};

} // namespace detail
} // namespace forge::net::p2p
