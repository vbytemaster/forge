module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.db.rocksdb.plugin;

import forge.asio.task_scheduler;
import forge.exceptions;
import forge.plugins.db.rocksdb.exceptions;
import forge.rocksdb.transaction;

#include "details/plugin_impl.hxx"
#include "details/transaction_impl.hxx"

namespace forge::plugins::db::rocksdb {

native_transaction::native_transaction(forge::rocksdb::transaction transaction, forge::asio::task_scheduler& scheduler)
   : scheduler_{&scheduler}
   , transaction_{std::move(transaction)} {
   if (scheduler_ == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "invalid RocksDB transaction construction");
   }
}

native_transaction::~native_transaction() = default;

boost::asio::awaitable<std::optional<std::vector<std::byte>>>
native_transaction::get(family column_family, std::vector<std::byte> key, read_options options) {
   co_return co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.get",
      [this, column_family = std::move(column_family), key = std::move(key), options] {
         const auto lock = std::scoped_lock{mutex_};
         return transaction_.get(std::move(column_family), std::move(key), options);
      });
}

boost::asio::awaitable<std::vector<entry>>
native_transaction::scan(family column_family, std::vector<std::byte> prefix, read_options options) {
   co_return co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.scan",
      [this, column_family = std::move(column_family), prefix = std::move(prefix), options] {
         const auto lock = std::scoped_lock{mutex_};
         return transaction_.scan(std::move(column_family), std::move(prefix), options);
      });
}

boost::asio::awaitable<scan_result> native_transaction::scan_page(family column_family, scan_request request) {
   co_return co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.scan_page",
      [this, column_family = std::move(column_family), request = std::move(request)] {
         const auto lock = std::scoped_lock{mutex_};
         return transaction_.scan_page(std::move(column_family), std::move(request));
      });
}

boost::asio::awaitable<void>
native_transaction::lock(family column_family, std::vector<std::byte> key, read_options options) {
   co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.lock",
      [this, column_family = std::move(column_family), key = std::move(key), options] {
         const auto lock = std::scoped_lock{mutex_};
         transaction_.lock(std::move(column_family), std::move(key), options);
      });
}

boost::asio::awaitable<void>
native_transaction::put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value) {
   co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.put",
      [this, column_family = std::move(column_family), key = std::move(key), value = std::move(value)] {
         const auto lock = std::scoped_lock{mutex_};
         transaction_.put(std::move(column_family), std::move(key), std::move(value));
      });
}

boost::asio::awaitable<void> native_transaction::erase(family column_family, std::vector<std::byte> key) {
   co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.erase",
      [this, column_family = std::move(column_family), key = std::move(key)] {
         const auto lock = std::scoped_lock{mutex_};
         transaction_.erase(std::move(column_family), std::move(key));
      });
}

boost::asio::awaitable<void> native_transaction::commit() {
   co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.commit",
      [this] {
         const auto lock = std::scoped_lock{mutex_};
         transaction_.commit();
         completed_ = true;
      });
}

boost::asio::awaitable<void> native_transaction::rollback() {
   co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.rollback",
      [this] {
         const auto lock = std::scoped_lock{mutex_};
         transaction_.rollback();
         completed_ = true;
      });
}

} // namespace forge::plugins::db::rocksdb
