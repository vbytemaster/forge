#pragma once

#include "transaction_owner.hxx"

namespace forge::plugins::db::rocksdb {

struct native_transaction_state;

class native_transaction final : public transaction, public native_transaction_control {
 public:
   native_transaction(forge::rocksdb::transaction transaction, std::shared_ptr<native_transaction_owner> owner);
   ~native_transaction() override;

   void release_native() noexcept override;

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
   std::shared_ptr<native_transaction_state> state_;
};

} // namespace forge::plugins::db::rocksdb
