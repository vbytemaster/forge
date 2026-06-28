module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
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

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>
#include <rocksdb/utilities/transaction_db.h>

module forge.plugins.db.rocksdb.plugin;

import forge.asio.task_scheduler;
import forge.exceptions;
import forge.plugins.db.rocksdb.exceptions;

#include "details/plugin_impl.hxx"
#include "details/config.hxx"
#include "details/native_store_impl.hxx"

namespace forge::plugins::db::rocksdb::detail {

::rocksdb::Slice to_slice(std::span<const std::byte> bytes) {
   return ::rocksdb::Slice{
      reinterpret_cast<const char*>(bytes.data()),
      bytes.size(),
   };
}

std::vector<std::byte> to_bytes(const ::rocksdb::Slice& value) {
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
   if (!request.cursor.empty() && !starts_with(request.cursor, request.prefix)) {
      return result;
   }

   if (request.cursor.empty()) {
      iterator->Seek(to_slice(request.prefix));
   } else {
      iterator->Seek(to_slice(request.cursor));
      if (iterator->Valid()) {
         const auto key = to_bytes(iterator->key());
         if (key == request.cursor) {
            iterator->Next();
         }
      }
   }

   for (; iterator->Valid(); iterator->Next()) {
      auto key = to_bytes(iterator->key());
      if (!starts_with(key, request.prefix)) {
         break;
      }
      result.entries.push_back(entry{.key = std::move(key), .value = to_bytes(iterator->value())});
      if (result.entries.size() >= request.limit) {
         auto cursor = result.entries.back().key;
         iterator->Next();
         if (iterator->Valid()) {
            const auto next_key = to_bytes(iterator->key());
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

} // namespace forge::plugins::db::rocksdb::detail

namespace forge::plugins::db::rocksdb {

store_core::store_core(config value) : settings{std::move(value)} {
   auto names = detail::normalize_families(settings.column_families);
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

store_core::~store_core() {
   for (auto& [name, handle] : handles) {
      static_cast<void>(name);
      if (handle != nullptr && db != nullptr) {
         static_cast<void>(db->DestroyColumnFamilyHandle(handle));
      }
   }
}

::rocksdb::ColumnFamilyHandle* store_core::require_handle(const family& column_family) const {
   const auto iterator = handles.find(column_family.name);
   if (iterator == handles.end()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument,
                          "RocksDB column family is not open",
                          forge::exceptions::ctx("family", column_family.name));
   }
   return iterator->second;
}

} // namespace forge::plugins::db::rocksdb
