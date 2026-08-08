#pragma once

namespace forge::db::mdbx::detail {

class environment;

class driver_impl final : public std::enable_shared_from_this<driver_impl> {
 public:
   driver_impl(forge::asio::affine::executor executor,
               std::shared_ptr<environment> environment,
               std::thread::id affine_thread,
               std::shared_ptr<forge::asio::affine::lane> lane_owner = {});
   ~driver_impl();

   driver_impl(const driver_impl&) = delete;
   driver_impl& operator=(const driver_impl&) = delete;

   [[nodiscard]] const forge::asio::affine::executor& executor() const noexcept;
   [[nodiscard]] const std::shared_ptr<environment>& environment_handle() const noexcept;

   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>>
   open_transaction();
   std::unique_ptr<forge::db::core::session> open_snapshot();
   boost::asio::awaitable<void> create_checkpoint(std::string destination);
   boost::asio::awaitable<void> flush(bool sync);
   boost::asio::awaitable<void> close();
   boost::asio::awaitable<void> shutdown_managed_lane();

   void abort_sync(std::vector<MDBX_txn*> transactions) noexcept;

 private:
   void require_open() const;
   void close_sync() noexcept;
   void run_sync(std::string name, std::function<void()> operation) noexcept;

   std::shared_ptr<forge::asio::affine::lane> lane_owner_;
   forge::asio::affine::executor executor_;
   std::shared_ptr<environment> environment_;
   std::shared_ptr<forge::asio::gate> writer_gate_;
   std::thread::id affine_thread_;
   std::atomic_bool closed_ = false;
};

} // namespace forge::db::mdbx::detail
