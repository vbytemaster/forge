module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module forge.objectdb.rocksdb;

import forge.objectdb.store;
import forge.objectdb.cursor;
import forge.objectdb.types;
import forge.rocksdb.store;

export namespace forge::objectdb::rocksdb {

struct config {
   std::string path;
   std::string family = "objectdb";
   forge::rocksdb::write_options write;
   bool create_if_missing = true;
   bool create_missing_column_families = true;
};

class session final : public forge::objectdb::session {
 public:
   session(forge::rocksdb::transaction transaction, forge::rocksdb::family family);

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::objectdb::record_key key) override;
   boost::asio::awaitable<void> put(forge::objectdb::record_key key, std::vector<std::byte> value) override;
   boost::asio::awaitable<void> erase(forge::objectdb::record_key key) override;
   boost::asio::awaitable<forge::objectdb::record_scan_result> scan_page(forge::objectdb::key_range range,
                                                                         forge::objectdb::page_request request) override;
   boost::asio::awaitable<void> commit() override;
   boost::asio::awaitable<void> rollback() override;

 private:
   forge::rocksdb::transaction transaction_;
   forge::rocksdb::family family_;
};

class driver {
 public:
   explicit driver(config value);

   [[nodiscard]] forge::objectdb::session_factory<session> session_factory() const;

   void flush(bool sync = true);

 private:
   std::shared_ptr<forge::rocksdb::store> store_;
   forge::rocksdb::family family_;
   forge::rocksdb::write_options write_;
};

} // namespace forge::objectdb::rocksdb

export namespace forge::objectdb::rocksdb {

BOOST_DESCRIBE_STRUCT(config, (), (path, family, write, create_if_missing, create_missing_column_families))

} // namespace forge::objectdb::rocksdb
