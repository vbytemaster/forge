#pragma once

namespace forge::plugins::db::rocksdb {

struct native_transaction_owner {
   virtual ~native_transaction_owner() = default;

   [[nodiscard]] virtual std::pair<std::shared_ptr<forge::rocksdb::store>, forge::asio::task_scheduler*>
   require_running() const = 0;
};

} // namespace forge::plugins::db::rocksdb
