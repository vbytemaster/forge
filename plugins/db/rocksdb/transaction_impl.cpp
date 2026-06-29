module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
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
#include "details/transaction_owner.hxx"

namespace forge::plugins::db::rocksdb {

struct native_transaction_state {
   native_transaction_state(std::shared_ptr<native_transaction_owner> owner_value, forge::rocksdb::transaction transaction_value)
      : owner{std::move(owner_value)}
      , transaction{std::move(transaction_value)} {}

   std::shared_ptr<native_transaction_owner> owner;
   std::optional<forge::rocksdb::transaction> transaction;
   std::mutex mutex;
   bool closed_by_owner = false;
};

namespace {

std::pair<std::shared_ptr<forge::rocksdb::store>, forge::asio::task_scheduler*>
require_transaction_owner(const std::shared_ptr<native_transaction_state>& state) {
   if (state == nullptr || state->owner == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "invalid RocksDB transaction construction");
   }
   auto owner = std::shared_ptr<native_transaction_owner>{};
   {
      const auto lock = std::scoped_lock{state->mutex};
      if (state->closed_by_owner) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "rocksdb plugin is not started");
      }
      owner = state->owner;
   }
   return owner->require_running();
}

forge::asio::task_scheduler& require_transaction_scheduler(const std::shared_ptr<native_transaction_state>& state) {
   auto running = require_transaction_owner(state);
   return *running.second;
}

forge::rocksdb::transaction& require_native_transaction(native_transaction_state& state) {
   if (state.closed_by_owner || !state.transaction.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "rocksdb plugin is not started");
   }
   return *state.transaction;
}

} // namespace

native_transaction::native_transaction(forge::rocksdb::transaction transaction, std::shared_ptr<native_transaction_owner> owner)
   : state_{std::make_shared<native_transaction_state>(std::move(owner), std::move(transaction))} {
   if (state_->owner == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "invalid RocksDB transaction construction");
   }
}

native_transaction::~native_transaction() {
   release_native();
}

void native_transaction::release_native() noexcept {
   if (state_ == nullptr) {
      return;
   }

   const auto lock = std::scoped_lock{state_->mutex};
   state_->closed_by_owner = true;
   if (!state_->transaction.has_value()) {
      return;
   }

   try {
      state_->transaction->rollback();
   } catch (...) {
   }
   state_->transaction.reset();
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>>
native_transaction::get(family column_family, std::vector<std::byte> key, read_options options) {
   auto state = state_;
   auto& scheduler = require_transaction_scheduler(state);
   co_return co_await detail::run_scheduled(
      scheduler,
      "rocksdb.transaction.get",
      [state = std::move(state), column_family = std::move(column_family), key = std::move(key), options] {
         static_cast<void>(require_transaction_owner(state));
         const auto lock = std::scoped_lock{state->mutex};
         return require_native_transaction(*state).get(std::move(column_family), std::move(key), options);
      });
}

boost::asio::awaitable<std::vector<entry>>
native_transaction::scan(family column_family, std::vector<std::byte> prefix, read_options options) {
   auto state = state_;
   auto& scheduler = require_transaction_scheduler(state);
   co_return co_await detail::run_scheduled(
      scheduler,
      "rocksdb.transaction.scan",
      [state = std::move(state), column_family = std::move(column_family), prefix = std::move(prefix), options] {
         static_cast<void>(require_transaction_owner(state));
         const auto lock = std::scoped_lock{state->mutex};
         return require_native_transaction(*state).scan(std::move(column_family), std::move(prefix), options);
      });
}

boost::asio::awaitable<scan_result> native_transaction::scan_page(family column_family, scan_request request) {
   auto state = state_;
   auto& scheduler = require_transaction_scheduler(state);
   co_return co_await detail::run_scheduled(
      scheduler,
      "rocksdb.transaction.scan_page",
      [state = std::move(state), column_family = std::move(column_family), request = std::move(request)] {
         static_cast<void>(require_transaction_owner(state));
         const auto lock = std::scoped_lock{state->mutex};
         return require_native_transaction(*state).scan_page(std::move(column_family), std::move(request));
      });
}

boost::asio::awaitable<void>
native_transaction::lock(family column_family, std::vector<std::byte> key, read_options options) {
   auto state = state_;
   auto& scheduler = require_transaction_scheduler(state);
   co_await detail::run_scheduled(
      scheduler,
      "rocksdb.transaction.lock",
      [state = std::move(state), column_family = std::move(column_family), key = std::move(key), options] {
         static_cast<void>(require_transaction_owner(state));
         const auto lock = std::scoped_lock{state->mutex};
         require_native_transaction(*state).lock(std::move(column_family), std::move(key), options);
      });
}

boost::asio::awaitable<void>
native_transaction::put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value) {
   auto state = state_;
   auto& scheduler = require_transaction_scheduler(state);
   co_await detail::run_scheduled(
      scheduler,
      "rocksdb.transaction.put",
      [state = std::move(state), column_family = std::move(column_family), key = std::move(key),
       value = std::move(value)] {
         static_cast<void>(require_transaction_owner(state));
         const auto lock = std::scoped_lock{state->mutex};
         require_native_transaction(*state).put(std::move(column_family), std::move(key), std::move(value));
      });
}

boost::asio::awaitable<void> native_transaction::erase(family column_family, std::vector<std::byte> key) {
   auto state = state_;
   auto& scheduler = require_transaction_scheduler(state);
   co_await detail::run_scheduled(
      scheduler,
      "rocksdb.transaction.erase",
      [state = std::move(state), column_family = std::move(column_family), key = std::move(key)] {
         static_cast<void>(require_transaction_owner(state));
         const auto lock = std::scoped_lock{state->mutex};
         require_native_transaction(*state).erase(std::move(column_family), std::move(key));
      });
}

boost::asio::awaitable<void> native_transaction::commit() {
   auto state = state_;
   auto& scheduler = require_transaction_scheduler(state);
   co_await detail::run_scheduled(
      scheduler,
      "rocksdb.transaction.commit",
      [state = std::move(state)] {
         static_cast<void>(require_transaction_owner(state));
         const auto lock = std::scoped_lock{state->mutex};
         require_native_transaction(*state).commit();
         state->transaction.reset();
      });
}

boost::asio::awaitable<void> native_transaction::rollback() {
   auto state = state_;
   auto& scheduler = require_transaction_scheduler(state);
   co_await detail::run_scheduled(
      scheduler,
      "rocksdb.transaction.rollback",
      [state = std::move(state)] {
         static_cast<void>(require_transaction_owner(state));
         const auto lock = std::scoped_lock{state->mutex};
         require_native_transaction(*state).rollback();
         state->transaction.reset();
      });
}

} // namespace forge::plugins::db::rocksdb
