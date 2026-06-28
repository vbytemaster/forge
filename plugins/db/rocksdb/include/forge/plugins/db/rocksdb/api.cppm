module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/macros.hpp>

#include <memory>
#include <optional>
#include <vector>

export module forge.plugins.db.rocksdb.api;

export import forge.plugins.db.rocksdb.types;

import forge.api.binding;

export namespace forge::plugins::db::rocksdb {

class transaction {
 public:
   virtual ~transaction() = default;

   virtual boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get(family column_family, std::vector<std::byte> key, read_options options = {}) = 0;

   virtual boost::asio::awaitable<std::vector<entry>>
   scan(family column_family, std::vector<std::byte> prefix, read_options options = {}) = 0;

   virtual boost::asio::awaitable<scan_result> scan_page(family column_family, scan_request request) = 0;

   virtual boost::asio::awaitable<void>
   lock(family column_family, std::vector<std::byte> key, read_options options = {}) = 0;

   virtual boost::asio::awaitable<void>
   put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value) = 0;

   virtual boost::asio::awaitable<void> erase(family column_family, std::vector<std::byte> key) = 0;
   virtual boost::asio::awaitable<void> commit() = 0;
   virtual boost::asio::awaitable<void> rollback() = 0;
};

class api : public forge::api::contract<api, forge::api::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get(family column_family, std::vector<std::byte> key, read_options options = {}) = 0;

   virtual boost::asio::awaitable<void>
   put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value, write_options options = {}) = 0;

   virtual boost::asio::awaitable<void>
   erase(family column_family, std::vector<std::byte> key, write_options options = {}) = 0;

   virtual boost::asio::awaitable<void> write(std::vector<operation> operations, write_options options = {}) = 0;

   virtual boost::asio::awaitable<std::vector<entry>>
   scan(family column_family, std::vector<std::byte> prefix, read_options options = {}) = 0;

   virtual boost::asio::awaitable<scan_result> scan_page(family column_family, scan_request request) = 0;

   virtual boost::asio::awaitable<std::shared_ptr<transaction>> begin(write_options options = {}) = 0;

   virtual boost::asio::awaitable<void> flush_wal(bool sync = true) = 0;
};

} // namespace forge::plugins::db::rocksdb

namespace db_api = ::forge::plugins::db::rocksdb;

export {
FORGE_API(db_api::api, FORGE_API_CONTRACT("forge.plugins.db.rocksdb", 1, 0))
}
