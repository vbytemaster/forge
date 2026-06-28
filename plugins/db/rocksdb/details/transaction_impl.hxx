#pragma once

namespace forge::plugins::db::rocksdb {

class native_transaction final : public transaction {
 public:
   native_transaction(
      std::shared_ptr<store_core> store,
      forge::asio::task_scheduler& scheduler,
      std::unique_ptr<::rocksdb::Transaction> transaction);
   ~native_transaction() override;

   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get(family column_family, std::vector<std::byte> key, read_options options) override;
   boost::asio::awaitable<std::vector<entry>>
   scan(family column_family, std::vector<std::byte> prefix, read_options options) override;
   boost::asio::awaitable<scan_result> scan_page(family column_family, scan_request request) override;
   boost::asio::awaitable<void> lock(family column_family, std::vector<std::byte> key, read_options options) override;
   boost::asio::awaitable<void>
   put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value) override;
   boost::asio::awaitable<void> erase(family column_family, std::vector<std::byte> key) override;
   boost::asio::awaitable<void> commit() override;
   boost::asio::awaitable<void> rollback() override;

 private:
   std::shared_ptr<store_core> store_;
   forge::asio::task_scheduler* scheduler_ = nullptr;
   std::unique_ptr<::rocksdb::Transaction> transaction_;
   std::mutex mutex_;
   bool completed_ = false;
};

} // namespace forge::plugins::db::rocksdb
