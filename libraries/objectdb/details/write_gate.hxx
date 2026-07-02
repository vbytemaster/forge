#pragma once

namespace forge::objectdb::detail {

class write_gate : public std::enable_shared_from_this<write_gate> {
 public:
   class ticket {
    public:
      ticket() = default;

      ticket(const ticket&) = delete;
      ticket& operator=(const ticket&) = delete;

      ticket(ticket&& other) noexcept;
      ticket& operator=(ticket&& other) noexcept;
      ~ticket();

      void release() noexcept;

    private:
      friend class write_gate;

      explicit ticket(std::shared_ptr<write_gate> gate);

      std::shared_ptr<write_gate> gate_;
   };

   boost::asio::awaitable<ticket> acquire();

 private:
   friend class ticket;

   void release_one() noexcept;

   std::mutex mutex_;
   bool held_ = false;
   std::deque<std::shared_ptr<boost::asio::steady_timer>> waiters_;
};

} // namespace forge::objectdb::detail
