#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

import forge.api.binding;
import forge.app.application_builder;
import forge.app.application_shell;
import forge.app.daemon;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.asio.task_scheduler;
import forge.config.component;
import forge.config.document;
import forge.config.value;
import forge.plugins.db.rocksdb.exceptions;
import forge.plugins.db.rocksdb.plugin;

namespace {

using forge::plugins::db::rocksdb::api;
using forge::plugins::db::rocksdb::config;
using forge::plugins::db::rocksdb::entry;
using forge::plugins::db::rocksdb::exceptions;
using forge::plugins::db::rocksdb::family;
using forge::plugins::db::rocksdb::make_key;
using forge::plugins::db::rocksdb::make_u64_key;
using forge::plugins::db::rocksdb::operation;
using forge::plugins::db::rocksdb::read_u64_be;
using forge::plugins::db::rocksdb::scan_request;
using forge::plugins::db::rocksdb::to_bytes;
using forge::plugins::db::rocksdb::to_string;
using forge::plugins::db::rocksdb::write_options;
namespace db = forge::plugins::db::rocksdb;

[[nodiscard]] const forge::config::field_descriptor&
require_field(const forge::config::component_descriptor& descriptor, const std::string& name) {
   const auto found = std::ranges::find_if(descriptor.fields, [&](const auto& field) {
      return field.name == name;
   });
   BOOST_REQUIRE(found != descriptor.fields.end());
   return *found;
}

[[nodiscard]] std::filesystem::path make_test_root() {
   const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
   auto root = std::filesystem::temp_directory_path() / ("forge_rocksdb_plugin_tests_" + std::to_string(nonce));
   std::filesystem::remove_all(root);
   return root;
}

struct root_guard {
   std::filesystem::path root = make_test_root();

   ~root_guard() {
      std::filesystem::remove_all(root);
   }
};

[[nodiscard]] forge::config::document document_for(const std::filesystem::path& path) {
   auto document = forge::config::document{};
   document.set("plugins.db.rocksdb.path", path.string());
   document.set(
      "plugins.db.rocksdb.column-families",
      forge::config::value::array_type{forge::config::value{"meta"}, forge::config::value{"data"}});
   return document;
}

[[nodiscard]] std::unique_ptr<forge::app::application_shell>
make_app(const std::filesystem::path& path,
         forge::asio::runtime_options runtime_options = {.worker_threads = 1, .thread_name = "rocksdb-test"},
         forge::asio::task_scheduler::options scheduler_options = {.max_blocking_tasks = 1, .max_pending_tasks = 256}) {
   auto builder = forge::app::application_builder{};
   builder.name("rocksdb-test")
      .runtime(runtime_options)
      .scheduler(scheduler_options)
      .plugin(forge::plugins::db::rocksdb::descriptor());

   auto app = std::move(builder).build();
   app->configure(document_for(path));
   forge::asio::blocking::run(app->runtime(), app->startup());
   return app;
}

} // namespace

BOOST_AUTO_TEST_CASE(rocksdb_descriptor_exposes_config_and_local_api) {
   const auto descriptor = forge::plugins::db::rocksdb::descriptor();
   BOOST_TEST(descriptor.id.value == "forge.plugins.db.rocksdb");
   BOOST_TEST(descriptor.dependencies.empty());

   auto instance = db::plugin{};
   const auto component = instance.describe_config();
   BOOST_REQUIRE(component.has_value());
   BOOST_TEST(component->section == "plugins.db.rocksdb");

   const auto& path = require_field(*component, "path");
   BOOST_TEST(!path.has_default);

   const auto& create_if_missing = require_field(*component, "create-if-missing");
   BOOST_TEST(create_if_missing.has_default);
   BOOST_TEST(std::get<bool>(create_if_missing.default_value.storage));

   const auto& create_missing_column_families = require_field(*component, "create-missing-column-families");
   BOOST_TEST(create_missing_column_families.has_default);
   BOOST_TEST(std::get<bool>(create_missing_column_families.default_value.storage));

   const auto api_descriptor = api::describe();
   BOOST_TEST(api_descriptor.id.value == "forge.plugins.db.rocksdb");
   BOOST_TEST(api_descriptor.methods.empty());
}

BOOST_AUTO_TEST_CASE(rocksdb_rejects_empty_path_and_empty_column_family) {
   auto runtime = forge::asio::runtime{};

   {
      auto instance = db::plugin{};
      auto document = forge::config::document{};
      document.set("plugins.db.rocksdb.column-families",
                   forge::config::value::array_type{forge::config::value{"meta"}});
      BOOST_CHECK_THROW(
         forge::asio::blocking::run(runtime, instance.configure(forge::config::component_view{document, "plugins.db.rocksdb"})),
         exceptions::invalid_config);
   }

   {
      auto instance = db::plugin{};
      auto document = forge::config::document{};
      document.set("plugins.db.rocksdb.path", std::string{"/tmp/forge-invalid-rocksdb"});
      document.set(
         "plugins.db.rocksdb.column-families",
         forge::config::value::array_type{forge::config::value{"meta"}, forge::config::value{""}});
      BOOST_CHECK_THROW(
         forge::asio::blocking::run(runtime, instance.configure(forge::config::component_view{document, "plugins.db.rocksdb"})),
         exceptions::invalid_config);
   }
}

BOOST_AUTO_TEST_CASE(rocksdb_put_get_delete_and_reopen) {
   const auto root = root_guard{};
   const auto db_path = root.root / "store";
   const auto meta = family{"meta"};

   {
      auto app = make_app(db_path);
      auto db = app->apis().get<api>(api::ref());

      forge::asio::blocking::run(app->runtime(), db->put(meta, make_key("schema"), to_bytes("1"), write_options{.sync = true}));
      const auto value = forge::asio::blocking::run(app->runtime(), db->get(meta, make_key("schema")));
      BOOST_REQUIRE(value.has_value());
      BOOST_TEST(to_string(*value) == "1");

      forge::asio::blocking::run(app->runtime(), db->erase(meta, make_key("schema"), write_options{.sync = true}));
      const auto erased = forge::asio::blocking::run(app->runtime(), db->get(meta, make_key("schema")));
      BOOST_TEST(!erased.has_value());

      forge::asio::blocking::run(app->runtime(), db->put(meta, make_key("schema"), to_bytes("2"), write_options{.sync = true}));
      forge::asio::blocking::run(app->runtime(), app->shutdown());
   }

   {
      auto app = make_app(db_path);
      auto db = app->apis().get<api>(api::ref());
      const auto persisted = forge::asio::blocking::run(app->runtime(), db->get(meta, make_key("schema")));
      BOOST_REQUIRE(persisted.has_value());
      BOOST_TEST(to_string(*persisted) == "2");
      forge::asio::blocking::run(app->runtime(), app->shutdown());
   }
}

BOOST_AUTO_TEST_CASE(rocksdb_scans_ordered_big_endian_prefix) {
   const auto root = root_guard{};
   auto app = make_app(root.root / "store");
   auto db = app->apis().get<api>(api::ref());
   const auto data = family{"data"};
   auto prefix = to_bytes("inode:");

   auto key_one = prefix;
   forge::plugins::db::rocksdb::append_u64_be(key_one, 1);
   auto key_two = prefix;
   forge::plugins::db::rocksdb::append_u64_be(key_two, 2);
   auto other = to_bytes("other:");
   forge::plugins::db::rocksdb::append_u64_be(other, 1);

   forge::asio::blocking::run(app->runtime(), db->put(data, key_two, to_bytes("two")));
   forge::asio::blocking::run(app->runtime(), db->put(data, other, to_bytes("other")));
   forge::asio::blocking::run(app->runtime(), db->put(data, key_one, to_bytes("one")));

   const auto scanned = forge::asio::blocking::run(app->runtime(), db->scan(data, prefix));
   BOOST_REQUIRE_EQUAL(scanned.size(), 2U);
   BOOST_TEST(read_u64_be(std::span<const std::byte>{scanned[0].key.data(), scanned[0].key.size()}.last(8)) == 1U);
   BOOST_TEST(to_string(scanned[0].value) == "one");
   BOOST_TEST(read_u64_be(std::span<const std::byte>{scanned[1].key.data(), scanned[1].key.size()}.last(8)) == 2U);
   BOOST_TEST(to_string(scanned[1].value) == "two");

   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

BOOST_AUTO_TEST_CASE(rocksdb_scan_page_bounds_prefix_with_cursor) {
   const auto root = root_guard{};
   auto app = make_app(root.root / "store");
   auto db = app->apis().get<api>(api::ref());
   const auto data = family{"data"};
   auto prefix = to_bytes("block:");

   for (auto value : {1U, 2U, 3U}) {
      auto key = prefix;
      forge::plugins::db::rocksdb::append_u64_be(key, value);
      forge::asio::blocking::run(app->runtime(), db->put(data, key, to_bytes(std::to_string(value))));
   }
   auto other = to_bytes("other:");
   forge::plugins::db::rocksdb::append_u64_be(other, 1);
   forge::asio::blocking::run(app->runtime(), db->put(data, other, to_bytes("other")));

   const auto first = forge::asio::blocking::run(
      app->runtime(),
      db->scan_page(data, scan_request{.prefix = prefix, .limit = 2}));
   BOOST_REQUIRE_EQUAL(first.entries.size(), 2U);
   BOOST_TEST(to_string(first.entries[0].value) == "1");
   BOOST_TEST(to_string(first.entries[1].value) == "2");
   BOOST_TEST(!first.next_cursor.empty());

   const auto second = forge::asio::blocking::run(
      app->runtime(),
      db->scan_page(data, scan_request{.prefix = prefix, .cursor = first.next_cursor, .limit = 2}));
   BOOST_REQUIRE_EQUAL(second.entries.size(), 1U);
   BOOST_TEST(to_string(second.entries[0].value) == "3");
   BOOST_TEST(second.next_cursor.empty());

   auto transaction = forge::asio::blocking::run(app->runtime(), db->begin(write_options{.sync = true}));
   auto key_four = prefix;
   forge::plugins::db::rocksdb::append_u64_be(key_four, 4);
   forge::asio::blocking::run(app->runtime(), transaction->put(data, key_four, to_bytes("4")));
   const auto tx_page = forge::asio::blocking::run(
      app->runtime(),
      transaction->scan_page(data, scan_request{.prefix = prefix, .cursor = second.entries[0].key, .limit = 2}));
   BOOST_REQUIRE_EQUAL(tx_page.entries.size(), 1U);
   BOOST_TEST(to_string(tx_page.entries[0].value) == "4");
   forge::asio::blocking::run(app->runtime(), transaction->rollback());

   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

BOOST_AUTO_TEST_CASE(rocksdb_batch_and_transactions_are_atomic) {
   const auto root = root_guard{};
   auto app = make_app(root.root / "store");
   auto db = app->apis().get<api>(api::ref());
   const auto meta = family{"meta"};
   const auto data = family{"data"};

   forge::asio::blocking::run(
      app->runtime(),
      db->write(
         {
            operation::put(meta, make_key("schema"), to_bytes("1")),
            operation::put(data, make_u64_key(42), to_bytes("payload")),
         },
         write_options{.sync = true}));

   const auto schema = forge::asio::blocking::run(app->runtime(), db->get(meta, make_key("schema")));
   const auto payload = forge::asio::blocking::run(app->runtime(), db->get(data, make_u64_key(42)));
   BOOST_REQUIRE(schema.has_value());
   BOOST_REQUIRE(payload.has_value());
   BOOST_TEST(to_string(*schema) == "1");
   BOOST_TEST(to_string(*payload) == "payload");

   {
      auto transaction = forge::asio::blocking::run(app->runtime(), db->begin(write_options{.sync = true}));
      forge::asio::blocking::run(app->runtime(), transaction->put(meta, make_key("schema"), to_bytes("3")));
      forge::asio::blocking::run(app->runtime(), transaction->put(data, make_u64_key(7), to_bytes("rollback")));
      forge::asio::blocking::run(app->runtime(), transaction->rollback());
   }

   const auto rolled_back_schema = forge::asio::blocking::run(app->runtime(), db->get(meta, make_key("schema")));
   const auto rolled_back_payload = forge::asio::blocking::run(app->runtime(), db->get(data, make_u64_key(7)));
   BOOST_REQUIRE(rolled_back_schema.has_value());
   BOOST_TEST(to_string(*rolled_back_schema) == "1");
   BOOST_TEST(!rolled_back_payload.has_value());

   auto transaction = forge::asio::blocking::run(app->runtime(), db->begin(write_options{.sync = true}));
   forge::asio::blocking::run(app->runtime(), transaction->put(meta, make_key("schema"), to_bytes("4")));
   forge::asio::blocking::run(app->runtime(), transaction->put(data, make_u64_key(8), to_bytes("commit")));
   forge::asio::blocking::run(app->runtime(), transaction->commit());

   const auto committed_schema = forge::asio::blocking::run(app->runtime(), db->get(meta, make_key("schema")));
   const auto committed_payload = forge::asio::blocking::run(app->runtime(), db->get(data, make_u64_key(8)));
   BOOST_REQUIRE(committed_schema.has_value());
   BOOST_REQUIRE(committed_payload.has_value());
   BOOST_TEST(to_string(*committed_schema) == "4");
   BOOST_TEST(to_string(*committed_payload) == "commit");

   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

BOOST_AUTO_TEST_CASE(rocksdb_flush_wal_exposes_sync_boundary_for_composed_plugins) {
   const auto root = root_guard{};
   auto app = make_app(root.root / "store");
   auto db = app->apis().get<api>(api::ref());
   const auto meta = family{"meta"};

   forge::asio::blocking::run(app->runtime(), db->put(meta, make_key("fsync"), to_bytes("pending"), write_options{.sync = false}));
   forge::asio::blocking::run(app->runtime(), db->flush_wal(true));

   const auto value = forge::asio::blocking::run(app->runtime(), db->get(meta, make_key("fsync")));
   BOOST_REQUIRE(value.has_value());
   BOOST_TEST(to_string(*value) == "pending");
   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

BOOST_AUTO_TEST_CASE(rocksdb_transaction_scans_prefix_with_local_changes_and_auto_rollback) {
   const auto root = root_guard{};
   auto app = make_app(root.root / "store");
   auto db = app->apis().get<api>(api::ref());
   const auto data = family{"data"};
   auto prefix = to_bytes("dirty:");

   auto key_one = prefix;
   forge::plugins::db::rocksdb::append_u64_be(key_one, 1);
   auto key_two = prefix;
   forge::plugins::db::rocksdb::append_u64_be(key_two, 2);
   auto key_three = prefix;
   forge::plugins::db::rocksdb::append_u64_be(key_three, 3);

   forge::asio::blocking::run(app->runtime(), db->put(data, key_one, to_bytes("one")));
   forge::asio::blocking::run(app->runtime(), db->put(data, key_two, to_bytes("two")));

   {
      auto transaction = forge::asio::blocking::run(app->runtime(), db->begin(write_options{.sync = true}));
      forge::asio::blocking::run(app->runtime(), transaction->lock(data, prefix));
      forge::asio::blocking::run(app->runtime(), transaction->erase(data, key_one));
      forge::asio::blocking::run(app->runtime(), transaction->put(data, key_three, to_bytes("three")));

      const auto scanned = forge::asio::blocking::run(app->runtime(), transaction->scan(data, prefix));
      BOOST_REQUIRE_EQUAL(scanned.size(), 2U);
      BOOST_TEST(read_u64_be(std::span<const std::byte>{scanned[0].key.data(), scanned[0].key.size()}.last(8)) == 2U);
      BOOST_TEST(to_string(scanned[0].value) == "two");
      BOOST_TEST(read_u64_be(std::span<const std::byte>{scanned[1].key.data(), scanned[1].key.size()}.last(8)) == 3U);
      BOOST_TEST(to_string(scanned[1].value) == "three");
   }

   const auto persisted = forge::asio::blocking::run(app->runtime(), db->scan(data, prefix));
   BOOST_REQUIRE_EQUAL(persisted.size(), 2U);
   BOOST_TEST(read_u64_be(std::span<const std::byte>{persisted[0].key.data(), persisted[0].key.size()}.last(8)) == 1U);
   BOOST_TEST(read_u64_be(std::span<const std::byte>{persisted[1].key.data(), persisted[1].key.size()}.last(8)) == 2U);

   forge::asio::blocking::run(app->runtime(), app->shutdown());
}

BOOST_AUTO_TEST_CASE(rocksdb_operations_after_shutdown_fail_with_stopped) {
   const auto root = root_guard{};
   auto app = make_app(root.root / "store");
   auto db = app->apis().get<api>(api::ref());
   forge::asio::blocking::run(app->runtime(), app->shutdown());

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(app->runtime(), db->get(family{"meta"}, make_key("schema"))),
      exceptions::stopped);
}

BOOST_AUTO_TEST_CASE(rocksdb_scheduler_backpressure_rejects_saturated_work) {
   const auto root = root_guard{};
   auto app = make_app(
      root.root / "store",
      forge::asio::runtime_options{.worker_threads = 2, .thread_name = "rocksdb-backpressure-test"},
      forge::asio::task_scheduler::options{.max_blocking_tasks = 1, .max_pending_tasks = 1});
   auto db = app->apis().get<api>(api::ref());

   auto mutex = std::mutex{};
   auto ready = std::condition_variable{};
   auto release = std::condition_variable{};
   auto started = false;
   auto done = false;

   auto gate = app->scheduler().submit(
      forge::asio::task{
         .name = "rocksdb-test-gate",
         .work =
            [&] {
               auto lock = std::unique_lock{mutex};
               started = true;
               ready.notify_one();
               release.wait(lock, [&] {
                  return done;
               });
            },
      });

   {
      auto lock = std::unique_lock{mutex};
      ready.wait(lock, [&] {
         return started;
      });
   }

   auto pending = app->scheduler().submit_after(
      forge::asio::task{.name = "rocksdb-test-pending", .work = [] {}},
      std::chrono::hours{1});

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(app->runtime(), db->get(family{"meta"}, make_key("schema"))),
      forge::asio::exceptions::rejected);

   pending.cancel();
   {
      const auto lock = std::scoped_lock{mutex};
      done = true;
   }
   release.notify_one();

   forge::asio::blocking::run(app->runtime(), gate.wait());
   BOOST_CHECK_THROW(forge::asio::blocking::run(app->runtime(), pending.wait()), forge::asio::exceptions::canceled);
   forge::asio::blocking::run(app->runtime(), app->shutdown());
}
