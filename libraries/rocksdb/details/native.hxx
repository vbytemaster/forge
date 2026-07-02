#pragma once

namespace forge::rocksdb {

struct store::impl {
   explicit impl(config value);
   ~impl();

   [[nodiscard]] ::rocksdb::ColumnFamilyHandle* require_handle(const family& column_family) const;

   config settings;
   std::unique_ptr<::rocksdb::TransactionDB> db;
   std::unordered_map<std::string, ::rocksdb::ColumnFamilyHandle*> handles;
};

struct transaction::impl {
   impl(std::shared_ptr<store::impl> store_value, std::unique_ptr<::rocksdb::Transaction> transaction_value);

   std::shared_ptr<store::impl> store;
   std::unique_ptr<::rocksdb::Transaction> transaction;
   bool finished = false;
};

struct snapshot::impl {
   impl(std::shared_ptr<store::impl> store_value, const ::rocksdb::Snapshot* snapshot_value);
   ~impl();

   std::shared_ptr<store::impl> store;
   const ::rocksdb::Snapshot* snapshot = nullptr;
};

namespace detail {

[[nodiscard]] ::rocksdb::Slice to_slice(std::span<const std::byte> bytes);
[[nodiscard]] std::vector<std::byte> bytes_from_slice(const ::rocksdb::Slice& value);
[[nodiscard]] bool starts_with(std::span<const std::byte> value, std::span<const std::byte> prefix);
[[nodiscard]] scan_result read_scan_page(std::unique_ptr<::rocksdb::Iterator> iterator, scan_request request, std::string_view context);
[[nodiscard]] ::rocksdb::ReadOptions to_native_options(const read_options& options);
[[nodiscard]] ::rocksdb::ReadOptions to_native_options(const read_options& options, const ::rocksdb::Snapshot* snapshot);
[[nodiscard]] ::rocksdb::WriteOptions to_native_options(const write_options& options);
void throw_if_error(const ::rocksdb::Status& status, std::string_view context);

} // namespace detail

} // namespace forge::rocksdb
