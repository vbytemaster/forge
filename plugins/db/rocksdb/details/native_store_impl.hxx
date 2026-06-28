#pragma once

namespace forge::plugins::db::rocksdb::detail {

constexpr auto default_family_name = std::string_view{"default"};

[[nodiscard]] ::rocksdb::Slice to_slice(std::span<const std::byte> bytes);
[[nodiscard]] std::vector<std::byte> to_bytes(const ::rocksdb::Slice& value);
[[nodiscard]] bool starts_with(std::span<const std::byte> value, std::span<const std::byte> prefix);
[[nodiscard]] scan_result read_scan_page(std::unique_ptr<::rocksdb::Iterator> iterator, scan_request request, std::string_view context);
[[nodiscard]] ::rocksdb::ReadOptions to_native_options(const read_options& options);
[[nodiscard]] ::rocksdb::WriteOptions to_native_options(const write_options& options);
void throw_if_error(const ::rocksdb::Status& status, std::string_view context);

} // namespace forge::plugins::db::rocksdb::detail

namespace forge::plugins::db::rocksdb {

struct store_core final {
   explicit store_core(config value);
   ~store_core();

   [[nodiscard]] ::rocksdb::ColumnFamilyHandle* require_handle(const family& column_family) const;

   config settings;
   std::unique_ptr<::rocksdb::TransactionDB> db;
   std::unordered_map<std::string, ::rocksdb::ColumnFamilyHandle*> handles;
};

} // namespace forge::plugins::db::rocksdb
