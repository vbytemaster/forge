module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

module forge.objectdb.rocksdb;

namespace forge::objectdb::rocksdb {

session::session(forge::rocksdb::transaction transaction, forge::rocksdb::family family)
    : transaction_{std::move(transaction)}, family_{std::move(family)} {}

forge::objectdb::capabilities session::capabilities() const noexcept {
   return forge::objectdb::capabilities{.snapshot_reads = false, .writes = true};
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> session::get(forge::objectdb::record_key key) {
   co_return transaction_.get(family_, key.bytes());
}

boost::asio::awaitable<void> session::put(forge::objectdb::record_key key, std::vector<std::byte> value) {
   transaction_.put(family_, key.bytes(), std::move(value));
   co_return;
}

boost::asio::awaitable<void> session::erase(forge::objectdb::record_key key) {
   transaction_.erase(family_, key.bytes());
   co_return;
}

boost::asio::awaitable<forge::objectdb::record_page> session::scan_page(forge::objectdb::record_range range,
                                                                        forge::objectdb::page_request request) {
   forge::objectdb::validate_page_request(request);

   auto scan = transaction_.scan_page(
      family_,
      forge::rocksdb::scan_request{
         .prefix = range.prefix.empty() ? range.begin.bytes() : range.prefix.bytes(),
         .lower_bound = range.begin.bytes(),
         .cursor = request.after ? request.after->boundary.bytes() : std::vector<std::byte>{},
         .limit = request.limit,
      });

   auto result = forge::objectdb::record_page{};
   auto last_returned = std::optional<forge::objectdb::record_key>{};
   auto stopped_at_range_end = false;
   for (auto& entry : scan.entries) {
      auto key = forge::objectdb::record_key{std::move(entry.key)};
      if (range.has_end && !(key.bytes() < range.end.bytes())) {
         stopped_at_range_end = true;
         break;
      }
      last_returned = key;
      result.entries.push_back(forge::objectdb::record_entry{.key = std::move(key), .value = std::move(entry.value)});
   }

   if (!stopped_at_range_end && !scan.next_cursor.empty() && last_returned.has_value()) {
      result.next = std::move(last_returned);
   }
   co_return result;
}

boost::asio::awaitable<void> session::commit() {
   transaction_.commit();
   co_return;
}

boost::asio::awaitable<void> session::rollback() {
   transaction_.rollback();
   co_return;
}

snapshot_session::snapshot_session(forge::rocksdb::snapshot snapshot, forge::rocksdb::family family)
    : snapshot_{std::move(snapshot)}, family_{std::move(family)} {}

forge::objectdb::capabilities snapshot_session::capabilities() const noexcept {
   return forge::objectdb::capabilities{.snapshot_reads = true, .writes = false};
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> snapshot_session::get(forge::objectdb::record_key key) {
   co_return snapshot_.get(family_, key.bytes());
}

boost::asio::awaitable<void> snapshot_session::put(forge::objectdb::record_key, std::vector<std::byte>) {
   FORGE_THROW_EXCEPTION(forge::objectdb::exceptions::unsupported_operation, "objectdb RocksDB snapshot is read-only");
}

boost::asio::awaitable<void> snapshot_session::erase(forge::objectdb::record_key) {
   FORGE_THROW_EXCEPTION(forge::objectdb::exceptions::unsupported_operation, "objectdb RocksDB snapshot is read-only");
}

boost::asio::awaitable<forge::objectdb::record_page> snapshot_session::scan_page(
   forge::objectdb::record_range range,
   forge::objectdb::page_request request) {
   forge::objectdb::validate_page_request(request);

   auto scan = snapshot_.scan_page(
      family_,
      forge::rocksdb::scan_request{
         .prefix = range.prefix.empty() ? range.begin.bytes() : range.prefix.bytes(),
         .lower_bound = range.begin.bytes(),
         .cursor = request.after ? request.after->boundary.bytes() : std::vector<std::byte>{},
         .limit = request.limit,
      });

   auto result = forge::objectdb::record_page{};
   auto last_returned = std::optional<forge::objectdb::record_key>{};
   auto stopped_at_range_end = false;
   for (auto& entry : scan.entries) {
      auto key = forge::objectdb::record_key{std::move(entry.key)};
      if (range.has_end && !(key.bytes() < range.end.bytes())) {
         stopped_at_range_end = true;
         break;
      }
      last_returned = key;
      result.entries.push_back(forge::objectdb::record_entry{.key = std::move(key), .value = std::move(entry.value)});
   }

   if (!stopped_at_range_end && !scan.next_cursor.empty() && last_returned.has_value()) {
      result.next = std::move(last_returned);
   }
   co_return result;
}

boost::asio::awaitable<void> snapshot_session::commit() {
   FORGE_THROW_EXCEPTION(forge::objectdb::exceptions::unsupported_operation, "objectdb RocksDB snapshot cannot commit");
}

boost::asio::awaitable<void> snapshot_session::rollback() {
   co_return;
}

driver::driver(config value)
    : store_{std::make_shared<forge::rocksdb::store>(forge::rocksdb::config{
         .path = std::move(value.path),
         .column_families = {value.family},
         .create_if_missing = value.create_if_missing,
         .create_missing_column_families = value.create_missing_column_families,
      })},
      family_{std::move(value.family)},
      write_{value.write} {}

forge::objectdb::session_factory<session> driver::session_factory() const {
   return forge::objectdb::session_factory<session>{
      [store = store_, family = family_, write = write_]() -> boost::asio::awaitable<std::unique_ptr<session>> {
         co_return std::make_unique<session>(store->begin(write), family);
      }};
}

forge::objectdb::session_factory<snapshot_session> driver::snapshot_factory() const {
   return forge::objectdb::session_factory<snapshot_session>{
      [store = store_, family = family_]() -> boost::asio::awaitable<std::unique_ptr<snapshot_session>> {
         co_return std::make_unique<snapshot_session>(store->begin_snapshot(), family);
      }};
}

void driver::flush(bool sync) {
   store_->flush_wal(sync);
}

} // namespace forge::objectdb::rocksdb
