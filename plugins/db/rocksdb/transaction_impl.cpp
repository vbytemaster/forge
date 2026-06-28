module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rocksdb/iterator.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/transaction_db.h>

module forge.plugins.db.rocksdb.plugin;

import forge.asio.task_scheduler;
import forge.exceptions;
import forge.plugins.db.rocksdb.exceptions;

#include "details/plugin_impl.hxx"
#include "details/native_store_impl.hxx"
#include "details/transaction_impl.hxx"

namespace forge::plugins::db::rocksdb {

native_transaction::native_transaction(
   std::shared_ptr<store_core> store,
   forge::asio::task_scheduler& scheduler,
   std::unique_ptr<::rocksdb::Transaction> transaction)
   : store_{std::move(store)}
   , scheduler_{&scheduler}
   , transaction_{std::move(transaction)} {
   if (store_ == nullptr || scheduler_ == nullptr || transaction_ == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "invalid RocksDB transaction construction");
   }
}

native_transaction::~native_transaction() {
   const auto lock = std::scoped_lock{mutex_};
   if (transaction_ != nullptr && !completed_) {
      static_cast<void>(transaction_->Rollback());
   }
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>>
native_transaction::get(family column_family, std::vector<std::byte> key, read_options options) {
   co_return co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.get",
      [this, column_family = std::move(column_family), key = std::move(key), options] {
         const auto lock = std::scoped_lock{mutex_};
         std::string value;
         const auto status = transaction_->Get(
            detail::to_native_options(options),
            store_->require_handle(column_family),
            detail::to_slice(key),
            &value);
         if (status.IsNotFound()) {
            return std::optional<std::vector<std::byte>>{};
         }
         detail::throw_if_error(status, "failed to get RocksDB transaction value");
         std::vector<std::byte> bytes;
         bytes.resize(value.size());
         std::memcpy(bytes.data(), value.data(), value.size());
         return std::optional<std::vector<std::byte>>{std::move(bytes)};
      });
}

boost::asio::awaitable<std::vector<entry>>
native_transaction::scan(family column_family, std::vector<std::byte> prefix, read_options options) {
   co_return co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.scan",
      [this, column_family = std::move(column_family), prefix = std::move(prefix), options] {
         const auto lock = std::scoped_lock{mutex_};
         auto iterator = std::unique_ptr<::rocksdb::Iterator>{
            transaction_->GetIterator(detail::to_native_options(options), store_->require_handle(column_family)),
         };
         std::vector<entry> values;
         for (iterator->Seek(detail::to_slice(prefix)); iterator->Valid(); iterator->Next()) {
            auto key = detail::to_bytes(iterator->key());
            if (!detail::starts_with(key, prefix)) {
               break;
            }
            values.push_back(entry{.key = std::move(key), .value = detail::to_bytes(iterator->value())});
         }
         detail::throw_if_error(iterator->status(), "failed to scan RocksDB transaction prefix");
         return values;
      });
}

boost::asio::awaitable<scan_result> native_transaction::scan_page(family column_family, scan_request request) {
   co_return co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.scan_page",
      [this, column_family = std::move(column_family), request = std::move(request)] {
         const auto lock = std::scoped_lock{mutex_};
         auto iterator = std::unique_ptr<::rocksdb::Iterator>{
            transaction_->GetIterator(detail::to_native_options(request.options), store_->require_handle(column_family)),
         };
         return detail::read_scan_page(
            std::move(iterator),
            std::move(request),
            "failed to scan RocksDB transaction prefix page");
      });
}

boost::asio::awaitable<void>
native_transaction::lock(family column_family, std::vector<std::byte> key, read_options options) {
   co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.lock",
      [this, column_family = std::move(column_family), key = std::move(key), options] {
         const auto lock = std::scoped_lock{mutex_};
         std::string* value = nullptr;
         detail::throw_if_error(
            transaction_->GetForUpdate(
               detail::to_native_options(options),
               store_->require_handle(column_family),
               detail::to_slice(key),
               value,
               true),
            "failed to lock RocksDB transaction key");
      });
}

boost::asio::awaitable<void>
native_transaction::put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value) {
   co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.put",
      [this, column_family = std::move(column_family), key = std::move(key), value = std::move(value)] {
         const auto lock = std::scoped_lock{mutex_};
         detail::throw_if_error(
            transaction_->Put(store_->require_handle(column_family), detail::to_slice(key), detail::to_slice(value)),
            "failed to put RocksDB transaction value");
      });
}

boost::asio::awaitable<void> native_transaction::erase(family column_family, std::vector<std::byte> key) {
   co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.erase",
      [this, column_family = std::move(column_family), key = std::move(key)] {
         const auto lock = std::scoped_lock{mutex_};
         detail::throw_if_error(
            transaction_->Delete(store_->require_handle(column_family), detail::to_slice(key)),
            "failed to delete RocksDB transaction value");
      });
}

boost::asio::awaitable<void> native_transaction::commit() {
   co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.commit",
      [this] {
         const auto lock = std::scoped_lock{mutex_};
         detail::throw_if_error(transaction_->Commit(), "failed to commit RocksDB transaction");
         completed_ = true;
      });
}

boost::asio::awaitable<void> native_transaction::rollback() {
   co_await detail::run_scheduled(
      *scheduler_,
      "rocksdb.transaction.rollback",
      [this] {
         const auto lock = std::scoped_lock{mutex_};
         detail::throw_if_error(transaction_->Rollback(), "failed to rollback RocksDB transaction");
         completed_ = true;
      });
}

} // namespace forge::plugins::db::rocksdb
