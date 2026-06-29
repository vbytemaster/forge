module;

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.db.rocksdb.plugin;
import forge.asio.task_scheduler;
import forge.exceptions;
import forge.plugins.db.rocksdb.exceptions;
import forge.rocksdb.store;

#include "details/plugin_impl.hxx"
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
         return store->get(std::move(column_family), std::move(key), options);
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
         store->put(std::move(column_family), std::move(key), std::move(value), options);
      });
}

boost::asio::awaitable<void>
plugin::api_impl::erase(family column_family, std::vector<std::byte> key, write_options options) {
   auto [store, scheduler] = owner_->require_running();
   co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.erase",
      [store = std::move(store), column_family = std::move(column_family), key = std::move(key), options] {
         store->erase(std::move(column_family), std::move(key), options);
      });
}

boost::asio::awaitable<void> plugin::api_impl::write(std::vector<operation> operations, write_options options) {
   auto [store, scheduler] = owner_->require_running();
   co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.write",
      [store = std::move(store), operations = std::move(operations), options] {
         store->write(std::move(operations), options);
      });
}

boost::asio::awaitable<std::vector<entry>>
plugin::api_impl::scan(family column_family, std::vector<std::byte> prefix, read_options options) {
   auto [store, scheduler] = owner_->require_running();
   co_return co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.scan",
      [store = std::move(store), column_family = std::move(column_family), prefix = std::move(prefix), options] {
         return store->scan(std::move(column_family), std::move(prefix), options);
      });
}

boost::asio::awaitable<scan_result> plugin::api_impl::scan_page(family column_family, scan_request request) {
   auto [store, scheduler] = owner_->require_running();
   co_return co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.scan_page",
      [store = std::move(store), column_family = std::move(column_family), request = std::move(request)] {
         return store->scan_page(std::move(column_family), std::move(request));
      });
}

boost::asio::awaitable<std::shared_ptr<transaction>> plugin::api_impl::begin(write_options options) {
   auto [store, scheduler] = owner_->require_running();
   co_return co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.begin",
      [store = std::move(store), scheduler, options] {
         return std::shared_ptr<transaction>{
            std::make_shared<native_transaction>(store->begin(options), *scheduler),
         };
      });
}

boost::asio::awaitable<void> plugin::api_impl::flush_wal(bool sync) {
   auto [store, scheduler] = owner_->require_running();
   co_await detail::run_scheduled(
      *scheduler,
      "rocksdb.flush_wal",
      [store = std::move(store), sync] {
         store->flush_wal(sync);
      });
}

} // namespace forge::plugins::db::rocksdb
