module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
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

module forge.plugins.db.rocksdb.plugin;

import forge.api.binding;
import forge.app.plugin_context;
import forge.asio.task_scheduler;
import forge.config.component;
import forge.config.decode;
import forge.exceptions;
import forge.plugins.db.rocksdb.exceptions;
import forge.rocksdb.store;

#include "details/plugin_impl.hxx"
#include "details/config.hxx"
#include "details/transaction_owner.hxx"

namespace forge::plugins::db::rocksdb {

void plugin::impl::configure(config value) {
   detail::validate_config(value);
   release_transactions();
   auto lock = std::scoped_lock{mutex};
   settings = std::move(value);
   store.reset();
   current.store(phase::configured);
}

void plugin::impl::set_scheduler(forge::asio::task_scheduler& value) {
   auto lock = std::scoped_lock{mutex};
   scheduler = &value;
   current.store(phase::initialized);
}

void plugin::impl::open() {
   auto settings_copy = config{};
   {
      auto lock = std::scoped_lock{mutex};
      settings_copy = settings;
   }

   auto opened = std::make_shared<forge::rocksdb::store>(std::move(settings_copy));
   {
      auto lock = std::scoped_lock{mutex};
      store = std::move(opened);
      current.store(phase::started);
   }
}

void plugin::impl::request_stop() noexcept {
   current.store(phase::stopping);
}

void plugin::impl::close() {
   current.store(phase::stopping);
   release_transactions();
   auto lock = std::scoped_lock{mutex};
   store.reset();
   scheduler = nullptr;
   current.store(phase::stopped);
}

void plugin::impl::track_transaction(std::shared_ptr<native_transaction_control> transaction) {
   if (transaction == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "invalid RocksDB transaction construction");
   }

   auto lock = std::scoped_lock{mutex};
   if (current.load() != phase::started || store == nullptr || scheduler == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "rocksdb plugin is not started");
   }
   std::erase_if(transactions, [](const auto& existing) {
      return existing.expired();
   });
   transactions.push_back(std::move(transaction));
}

void plugin::impl::release_transactions() noexcept {
   auto active = std::vector<std::shared_ptr<native_transaction_control>>{};
   {
      auto lock = std::scoped_lock{mutex};
      active.reserve(transactions.size());
      for (auto& transaction : transactions) {
         if (auto state = transaction.lock()) {
            active.push_back(std::move(state));
         }
      }
      transactions.clear();
   }

   for (auto& state : active) {
      state->release_native();
   }
}

std::pair<std::shared_ptr<forge::rocksdb::store>, forge::asio::task_scheduler*> plugin::impl::require_running() const {
   auto lock = std::scoped_lock{mutex};
   if (current.load() != phase::started || store == nullptr || scheduler == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "rocksdb plugin is not started");
   }
   return {store, scheduler};
}

} // namespace forge::plugins::db::rocksdb

namespace forge::plugins::db::rocksdb::detail {

std::shared_ptr<plugin::impl> lifecycle::make_impl() {
   return std::make_shared<plugin::impl>();
}

std::optional<forge::config::component_descriptor> lifecycle::describe_config(const std::shared_ptr<plugin::impl>&) {
   return forge::config::describe_component<config>("plugins.db.rocksdb");
}

boost::asio::awaitable<void> lifecycle::configure(const std::shared_ptr<plugin::impl>& impl_, forge::config::component_view view) {
   impl_->configure(decode_config(view));
   co_return;
}

boost::asio::awaitable<void> lifecycle::provide(const std::shared_ptr<plugin::impl>& impl_, forge::api::provider& provider) {
   provider.install<api>(std::make_shared<plugin::api_impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> lifecycle::initialize(const std::shared_ptr<plugin::impl>& impl_, forge::app::plugin_context& context) {
   impl_->set_scheduler(context.scheduler());
   co_return;
}

boost::asio::awaitable<void> lifecycle::startup(const std::shared_ptr<plugin::impl>& impl_) {
   impl_->open();
   co_return;
}

void lifecycle::request_stop(const std::shared_ptr<plugin::impl>& impl_) noexcept {
   impl_->request_stop();
}

boost::asio::awaitable<void> lifecycle::shutdown(const std::shared_ptr<plugin::impl>& impl_) {
   impl_->close();
   co_return;
}

} // namespace forge::plugins::db::rocksdb::detail
