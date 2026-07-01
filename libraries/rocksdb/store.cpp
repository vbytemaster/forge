module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>
#include <rocksdb/utilities/transaction_db.h>

module forge.rocksdb.store;

import forge.exceptions;
import forge.rocksdb.exceptions;

#include "details/native.hxx"

namespace forge::rocksdb::detail {

::rocksdb::Slice to_slice(std::span<const std::byte> bytes) {
   return ::rocksdb::Slice{
      reinterpret_cast<const char*>(bytes.data()),
      bytes.size(),
   };
}

std::vector<std::byte> bytes_from_slice(const ::rocksdb::Slice& value) {
   std::vector<std::byte> bytes;
   bytes.resize(value.size());
   std::memcpy(bytes.data(), value.data(), value.size());
   return bytes;
}

bool starts_with(std::span<const std::byte> value, std::span<const std::byte> prefix) {
   return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
}

scan_result read_scan_page(std::unique_ptr<::rocksdb::Iterator> iterator, scan_request request, std::string_view context) {
   auto result = scan_result{};
   if (request.limit == 0) {
      return result;
   }
   if (!request.lower_bound.empty() && !starts_with(request.lower_bound, request.prefix)) {
      return result;
   }
   if (!request.cursor.empty() && !starts_with(request.cursor, request.prefix)) {
      return result;
   }

   if (request.cursor.empty()) {
      iterator->Seek(to_slice(request.lower_bound.empty() ? request.prefix : request.lower_bound));
   } else {
      iterator->Seek(to_slice(request.cursor));
      if (iterator->Valid()) {
         const auto key = bytes_from_slice(iterator->key());
         if (key == request.cursor) {
            iterator->Next();
         }
      }
   }

   for (; iterator->Valid(); iterator->Next()) {
      auto key = bytes_from_slice(iterator->key());
      if (!starts_with(key, request.prefix)) {
         break;
      }
      result.entries.push_back(entry{.key = std::move(key), .value = bytes_from_slice(iterator->value())});
      if (result.entries.size() >= request.limit) {
         auto cursor = result.entries.back().key;
         iterator->Next();
         if (iterator->Valid()) {
            const auto next_key = bytes_from_slice(iterator->key());
            if (starts_with(next_key, request.prefix)) {
               result.next_cursor = std::move(cursor);
            }
         }
         break;
      }
   }
   throw_if_error(iterator->status(), context);
   return result;
}

[[nodiscard]] status_code to_status_code(const ::rocksdb::Status& status) {
   if (status.ok()) {
      return status_code::ok;
   }
   if (status.IsNotFound()) {
      return status_code::not_found;
   }
   if (status.IsInvalidArgument()) {
      return status_code::invalid_argument;
   }
   if (status.IsCorruption()) {
      return status_code::corruption;
   }
   if (status.IsIOError()) {
      return status_code::io_error;
   }
   if (status.IsTimedOut()) {
      return status_code::timed_out;
   }
   if (status.IsBusy()) {
      return status_code::busy;
   }
   return status_code::unknown;
}

[[noreturn]] void throw_status(status_code code, std::string message) {
   switch (code) {
      case status_code::invalid_argument:
         FORGE_THROW_EXCEPTION(exceptions::invalid_argument, std::move(message));
      case status_code::corruption:
         FORGE_THROW_EXCEPTION(exceptions::corruption, std::move(message));
      case status_code::io_error:
         FORGE_THROW_EXCEPTION(exceptions::io_error, std::move(message));
      case status_code::timed_out:
         FORGE_THROW_EXCEPTION(exceptions::timed_out, std::move(message));
      case status_code::busy:
         FORGE_THROW_EXCEPTION(exceptions::busy, std::move(message));
      case status_code::ok:
      case status_code::not_found:
      case status_code::unknown:
         break;
   }
   FORGE_THROW_EXCEPTION(exceptions::internal_error, std::move(message));
}

void throw_if_error(const ::rocksdb::Status& status, std::string_view context) {
   if (status.ok()) {
      return;
   }
   throw_status(to_status_code(status), std::string{context} + ": " + status.ToString());
}

::rocksdb::ReadOptions to_native_options(const read_options& options) {
   ::rocksdb::ReadOptions native;
   native.verify_checksums = options.verify_checksums;
   native.fill_cache = options.fill_cache;
   return native;
}

::rocksdb::WriteOptions to_native_options(const write_options& options) {
   ::rocksdb::WriteOptions native;
   native.sync = options.sync;
   native.disableWAL = options.disable_wal;
   return native;
}

} // namespace forge::rocksdb::detail

namespace forge::rocksdb {

store::impl::impl(config value) : settings{std::move(value)} {
   if (settings.path.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "RocksDB path must not be empty");
   }

   auto names = settings.column_families;
   names.erase(std::remove_if(names.begin(), names.end(), [](const std::string& value) {
      return value == "default";
   }), names.end());
   std::ranges::sort(names);
   names.erase(std::unique(names.begin(), names.end()), names.end());
   names.insert(names.begin(), "default");

   const auto path = std::filesystem::path{settings.path};
   if (const auto parent = path.parent_path(); !parent.empty()) {
      std::filesystem::create_directories(parent);
   }

   auto db_options = ::rocksdb::DBOptions{};
   db_options.create_if_missing = settings.create_if_missing;
   db_options.create_missing_column_families = settings.create_missing_column_families;

   std::vector<std::string> existing_names;
   const auto list_status = ::rocksdb::DB::ListColumnFamilies(db_options, path.string(), &existing_names);
   if (list_status.ok()) {
      for (const auto& name : existing_names) {
         if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
         }
      }
   } else if (!settings.create_if_missing && !list_status.IsIOError()) {
      detail::throw_if_error(list_status, "failed to list RocksDB column families");
   }

   std::vector<::rocksdb::ColumnFamilyDescriptor> descriptors;
   descriptors.reserve(names.size());
   for (const auto& name : names) {
      descriptors.emplace_back(name, ::rocksdb::ColumnFamilyOptions{});
   }

   std::vector<::rocksdb::ColumnFamilyHandle*> opened_handles;
   ::rocksdb::TransactionDB* opened_db = nullptr;
   const auto open_status = ::rocksdb::TransactionDB::Open(
      db_options,
      ::rocksdb::TransactionDBOptions{},
      path.string(),
      descriptors,
      &opened_handles,
      &opened_db);
   detail::throw_if_error(open_status, "failed to open RocksDB TransactionDB store");

   db.reset(opened_db);
   for (std::size_t index = 0; index < names.size(); ++index) {
      handles.emplace(names[index], opened_handles[index]);
   }
}

store::impl::~impl() {
   for (auto& [name, handle] : handles) {
      static_cast<void>(name);
      if (handle != nullptr && db != nullptr) {
         static_cast<void>(db->DestroyColumnFamilyHandle(handle));
      }
   }
}

::rocksdb::ColumnFamilyHandle* store::impl::require_handle(const family& column_family) const {
   const auto iterator = handles.find(column_family.name);
   if (iterator == handles.end()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument,
                          "RocksDB column family is not open",
                          forge::exceptions::ctx("family", column_family.name));
   }
   return iterator->second;
}

transaction::impl::impl(std::shared_ptr<store::impl> store_value, std::unique_ptr<::rocksdb::Transaction> transaction_value)
    : store{std::move(store_value)}, transaction{std::move(transaction_value)} {}

transaction::transaction(std::unique_ptr<impl> impl_value) : impl_{std::move(impl_value)} {}

transaction::~transaction() {
   rollback_if_active();
}

transaction::transaction(transaction&&) noexcept = default;

transaction& transaction::operator=(transaction&& other) noexcept {
   if (this != &other) {
      rollback_if_active();
      impl_ = std::move(other.impl_);
   }
   return *this;
}

void transaction::rollback_if_active() noexcept {
   if (impl_ != nullptr && !impl_->finished && impl_->transaction != nullptr) {
      static_cast<void>(impl_->transaction->Rollback());
      impl_->finished = true;
   }
}

void transaction::ensure_active(std::string_view context) const {
   if (impl_ == nullptr || impl_->finished || impl_->transaction == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, std::string{context} + ": RocksDB transaction is closed");
   }
}

std::optional<std::vector<std::byte>>
transaction::get(family column_family, std::vector<std::byte> key, read_options options) {
   ensure_active("failed to get RocksDB transaction value");
   std::string value;
   const auto status = impl_->transaction->Get(
      detail::to_native_options(options),
      impl_->store->require_handle(column_family),
      detail::to_slice(key),
      &value);
   if (status.IsNotFound()) {
      return std::nullopt;
   }
   detail::throw_if_error(status, "failed to get RocksDB transaction value");
   auto bytes = std::vector<std::byte>{};
   bytes.resize(value.size());
   std::memcpy(bytes.data(), value.data(), value.size());
   return bytes;
}

std::vector<entry> transaction::scan(family column_family, std::vector<std::byte> prefix, read_options options) {
   ensure_active("failed to scan RocksDB transaction prefix");
   auto iterator = std::unique_ptr<::rocksdb::Iterator>{
      impl_->transaction->GetIterator(detail::to_native_options(options), impl_->store->require_handle(column_family)),
   };

   auto values = std::vector<entry>{};
   for (iterator->Seek(detail::to_slice(prefix)); iterator->Valid(); iterator->Next()) {
      auto key = detail::bytes_from_slice(iterator->key());
      if (!detail::starts_with(key, prefix)) {
         break;
      }
      values.push_back(entry{.key = std::move(key), .value = detail::bytes_from_slice(iterator->value())});
   }
   detail::throw_if_error(iterator->status(), "failed to scan RocksDB transaction prefix");
   return values;
}

scan_result transaction::scan_page(family column_family, scan_request request) {
   ensure_active("failed to scan RocksDB transaction prefix page");
   auto iterator = std::unique_ptr<::rocksdb::Iterator>{
      impl_->transaction->GetIterator(
         detail::to_native_options(request.options),
         impl_->store->require_handle(column_family)),
   };
   return detail::read_scan_page(std::move(iterator), std::move(request), "failed to scan RocksDB transaction prefix page");
}

void transaction::lock(family column_family, std::vector<std::byte> key, read_options options) {
   ensure_active("failed to lock RocksDB transaction key");
   const auto status = impl_->transaction->GetForUpdate(
      detail::to_native_options(options),
      impl_->store->require_handle(column_family),
      detail::to_slice(key),
      static_cast<std::string*>(nullptr));
   if (status.IsNotFound()) {
      return;
   }
   detail::throw_if_error(status, "failed to lock RocksDB transaction key");
}

void transaction::put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value) {
   ensure_active("failed to put RocksDB transaction value");
   detail::throw_if_error(
      impl_->transaction->Put(
         impl_->store->require_handle(column_family),
         detail::to_slice(key),
         detail::to_slice(value)),
      "failed to put RocksDB transaction value");
}

void transaction::erase(family column_family, std::vector<std::byte> key) {
   ensure_active("failed to delete RocksDB transaction value");
   detail::throw_if_error(
      impl_->transaction->Delete(impl_->store->require_handle(column_family), detail::to_slice(key)),
      "failed to delete RocksDB transaction value");
}

void transaction::commit() {
   ensure_active("failed to commit RocksDB transaction");
   detail::throw_if_error(impl_->transaction->Commit(), "failed to commit RocksDB transaction");
   impl_->finished = true;
}

void transaction::rollback() {
   ensure_active("failed to rollback RocksDB transaction");
   detail::throw_if_error(impl_->transaction->Rollback(), "failed to rollback RocksDB transaction");
   impl_->finished = true;
}

store::store(config value) : impl_{std::make_shared<impl>(std::move(value))} {}
store::~store() = default;
store::store(store&&) noexcept = default;
store& store::operator=(store&&) noexcept = default;

std::optional<std::vector<std::byte>>
store::get(family column_family, std::vector<std::byte> key, read_options options) {
   std::string value;
   const auto status = impl_->db->Get(
      detail::to_native_options(options),
      impl_->require_handle(column_family),
      detail::to_slice(key),
      &value);
   if (status.IsNotFound()) {
      return std::nullopt;
   }
   detail::throw_if_error(status, "failed to get RocksDB value");
   auto bytes = std::vector<std::byte>{};
   bytes.resize(value.size());
   std::memcpy(bytes.data(), value.data(), value.size());
   return bytes;
}

void store::put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value, write_options options) {
   auto transaction = begin(options);
   transaction.put(std::move(column_family), std::move(key), std::move(value));
   transaction.commit();
}

void store::erase(family column_family, std::vector<std::byte> key, write_options options) {
   auto transaction = begin(options);
   transaction.erase(std::move(column_family), std::move(key));
   transaction.commit();
}

void store::write(std::vector<operation> operations, write_options options) {
   auto transaction = begin(options);
   for (const auto& operation : operations) {
      switch (operation.kind) {
         case operation_kind::put:
            transaction.put(operation.column_family, operation.key, operation.value);
            break;
         case operation_kind::erase:
            transaction.erase(operation.column_family, operation.key);
            break;
      }
   }
   transaction.commit();
}

std::vector<entry> store::scan(family column_family, std::vector<std::byte> prefix, read_options options) {
   auto iterator = std::unique_ptr<::rocksdb::Iterator>{
      impl_->db->NewIterator(detail::to_native_options(options), impl_->require_handle(column_family)),
   };

   std::vector<entry> values;
   for (iterator->Seek(detail::to_slice(prefix)); iterator->Valid(); iterator->Next()) {
      auto key = detail::bytes_from_slice(iterator->key());
      if (!detail::starts_with(key, prefix)) {
         break;
      }
      values.push_back(entry{.key = std::move(key), .value = detail::bytes_from_slice(iterator->value())});
   }
   detail::throw_if_error(iterator->status(), "failed to scan RocksDB prefix");
   return values;
}

scan_result store::scan_page(family column_family, scan_request request) {
   auto iterator = std::unique_ptr<::rocksdb::Iterator>{
      impl_->db->NewIterator(detail::to_native_options(request.options), impl_->require_handle(column_family)),
   };
   return detail::read_scan_page(std::move(iterator), std::move(request), "failed to scan RocksDB prefix page");
}

transaction store::begin(write_options options) {
   auto native = std::unique_ptr<::rocksdb::Transaction>{
      impl_->db->BeginTransaction(detail::to_native_options(options), ::rocksdb::TransactionOptions{}),
   };
   if (native == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::internal_error, "failed to begin RocksDB transaction");
   }
   return transaction{std::make_unique<transaction::impl>(impl_, std::move(native))};
}

void store::flush_wal(bool sync) {
   detail::throw_if_error(impl_->db->FlushWAL(sync), "failed to flush RocksDB WAL");
}

} // namespace forge::rocksdb
