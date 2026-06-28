module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <forge/exceptions/macros.hpp>
#include <cstring>
#include <exception>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
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
} // namespace forge::plugins::db::rocksdb

namespace forge::plugins::db::rocksdb {

plugin::api_impl::api_impl(std::shared_ptr<impl> owner) : owner_{std::move(owner)} {}

boost::asio::awaitable<std::optional<std::vector<std::byte>>>
plugin::api_impl::get(family column_family, std::vector<std::byte> key, read_options options) {
   auto [store, scheduler] = owner_->require_running();
   co_return co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.get",
      [store = std::move(store), column_family = std::move(column_family), key = std::move(key), options] {
         std::string value;
         const auto status = store->db->Get(
            detail::to_native_options(options),
            store->require_handle(column_family),
            detail::to_slice(key),
            &value);
         if (status.IsNotFound()) {
            return std::optional<std::vector<std::byte>>{};
         }
         detail::throw_if_error(status, "failed to get RocksDB value");
         std::vector<std::byte> bytes;
         bytes.resize(value.size());
         std::memcpy(bytes.data(), value.data(), value.size());
         return std::optional<std::vector<std::byte>>{std::move(bytes)};
      });
}

boost::asio::awaitable<void>
plugin::api_impl::put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value, write_options options) {
   auto [store, scheduler] = owner_->require_running();
   co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.put",
      [store = std::move(store), column_family = std::move(column_family), key = std::move(key),
       value = std::move(value), options] {
         auto transaction = std::unique_ptr<::rocksdb::Transaction>{
            store->db->BeginTransaction(detail::to_native_options(options), ::rocksdb::TransactionOptions{}),
         };
         if (transaction == nullptr) {
            FORGE_THROW_EXCEPTION(exceptions::internal_error, "failed to begin RocksDB transaction");
         }
         detail::throw_if_error(
            transaction->Put(store->require_handle(column_family), detail::to_slice(key), detail::to_slice(value)),
            "failed to put RocksDB transaction value");
         detail::throw_if_error(transaction->Commit(), "failed to commit RocksDB transaction");
      });
}

boost::asio::awaitable<void>
plugin::api_impl::erase(family column_family, std::vector<std::byte> key, write_options options) {
   auto [store, scheduler] = owner_->require_running();
   co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.erase",
      [store = std::move(store), column_family = std::move(column_family), key = std::move(key), options] {
         auto transaction = std::unique_ptr<::rocksdb::Transaction>{
            store->db->BeginTransaction(detail::to_native_options(options), ::rocksdb::TransactionOptions{}),
         };
         if (transaction == nullptr) {
            FORGE_THROW_EXCEPTION(exceptions::internal_error, "failed to begin RocksDB transaction");
         }
         detail::throw_if_error(
            transaction->Delete(store->require_handle(column_family), detail::to_slice(key)),
            "failed to delete RocksDB transaction value");
         detail::throw_if_error(transaction->Commit(), "failed to commit RocksDB transaction");
      });
}

boost::asio::awaitable<void> plugin::api_impl::write(std::vector<operation> operations, write_options options) {
   auto [store, scheduler] = owner_->require_running();
   co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.write",
      [store = std::move(store), operations = std::move(operations), options] {
         auto transaction = std::unique_ptr<::rocksdb::Transaction>{
            store->db->BeginTransaction(detail::to_native_options(options), ::rocksdb::TransactionOptions{}),
         };
         if (transaction == nullptr) {
            FORGE_THROW_EXCEPTION(exceptions::internal_error, "failed to begin RocksDB transaction");
         }
         for (const auto& operation : operations) {
            switch (operation.kind) {
               case operation_kind::put:
                  detail::throw_if_error(
                     transaction->Put(
                        store->require_handle(operation.column_family),
                        detail::to_slice(operation.key),
                        detail::to_slice(operation.value)),
                     "failed to put RocksDB transaction value");
                  break;
               case operation_kind::erase:
                  detail::throw_if_error(
                     transaction->Delete(store->require_handle(operation.column_family), detail::to_slice(operation.key)),
                     "failed to delete RocksDB transaction value");
                  break;
            }
         }
         detail::throw_if_error(transaction->Commit(), "failed to commit RocksDB transaction");
      });
}

boost::asio::awaitable<std::vector<entry>>
plugin::api_impl::scan(family column_family, std::vector<std::byte> prefix, read_options options) {
   auto [store, scheduler] = owner_->require_running();
   co_return co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.scan",
      [store = std::move(store), column_family = std::move(column_family), prefix = std::move(prefix), options] {
         auto iterator = std::unique_ptr<::rocksdb::Iterator>{
            store->db->NewIterator(detail::to_native_options(options), store->require_handle(column_family)),
         };

         std::vector<entry> values;
         for (iterator->Seek(detail::to_slice(prefix)); iterator->Valid(); iterator->Next()) {
            auto key = detail::to_bytes(iterator->key());
            if (!detail::starts_with(key, prefix)) {
               break;
            }
            values.push_back(entry{.key = std::move(key), .value = detail::to_bytes(iterator->value())});
         }
         detail::throw_if_error(iterator->status(), "failed to scan RocksDB prefix");
         return values;
      });
}

boost::asio::awaitable<scan_result> plugin::api_impl::scan_page(family column_family, scan_request request) {
   auto [store, scheduler] = owner_->require_running();
   co_return co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.scan_page",
      [store = std::move(store), column_family = std::move(column_family), request = std::move(request)] {
         auto iterator = std::unique_ptr<::rocksdb::Iterator>{
            store->db->NewIterator(detail::to_native_options(request.options), store->require_handle(column_family)),
         };
         return detail::read_scan_page(
            std::move(iterator),
            std::move(request),
            "failed to scan RocksDB prefix page");
      });
}

boost::asio::awaitable<std::shared_ptr<transaction>> plugin::api_impl::begin(write_options options) {
   auto [store, scheduler] = owner_->require_running();
   co_return co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.begin",
      [store = std::move(store), scheduler, options] {
         auto native = std::unique_ptr<::rocksdb::Transaction>{
            store->db->BeginTransaction(detail::to_native_options(options), ::rocksdb::TransactionOptions{}),
         };
         if (native == nullptr) {
            FORGE_THROW_EXCEPTION(exceptions::internal_error, "failed to begin RocksDB transaction");
         }
         return std::shared_ptr<transaction>{
            std::make_shared<native_transaction>(std::move(store), *scheduler, std::move(native)),
         };
      });
}

boost::asio::awaitable<void> plugin::api_impl::flush_wal(bool sync) {
   auto [store, scheduler] = owner_->require_running();
   co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.flush_wal",
      [store = std::move(store), sync] {
         detail::throw_if_error(store->db->FlushWAL(sync), "failed to flush RocksDB WAL");
      });
}

} // namespace forge::plugins::db::rocksdb
