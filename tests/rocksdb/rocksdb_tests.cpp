#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

import forge.rocksdb.exceptions;
import forge.rocksdb.store;

namespace {

using forge::rocksdb::config;
using forge::rocksdb::exceptions;
using forge::rocksdb::family;
using forge::rocksdb::make_key;
using forge::rocksdb::make_u64_key;
using forge::rocksdb::operation;
using forge::rocksdb::read_u64_be;
using forge::rocksdb::scan_request;
using forge::rocksdb::store;
using forge::rocksdb::to_bytes;
using forge::rocksdb::to_string;
using forge::rocksdb::write_options;

[[nodiscard]] std::filesystem::path make_test_root() {
   const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
   auto root = std::filesystem::temp_directory_path() / ("forge_rocksdb_tests_" + std::to_string(nonce));
   std::filesystem::remove_all(root);
   return root;
}

struct root_guard {
   std::filesystem::path root = make_test_root();

   ~root_guard() {
      std::filesystem::remove_all(root);
   }
};

[[nodiscard]] config config_for(const std::filesystem::path& path) {
   return config{
      .path = path.string(),
      .column_families = {"meta", "data", "meta", "default"},
   };
}

} // namespace

BOOST_AUTO_TEST_CASE(rocksdb_store_rejects_empty_path) {
   BOOST_CHECK_THROW((void)store{config{}}, exceptions::invalid_argument);
}

BOOST_AUTO_TEST_CASE(rocksdb_store_put_get_delete_and_reopen) {
   const auto root = root_guard{};
   const auto db_path = root.root / "store";
   const auto meta = family{"meta"};

   {
      auto db = store{config_for(db_path)};
      db.put(meta, make_key("schema"), to_bytes("1"), write_options{.sync = true});
      const auto value = db.get(meta, make_key("schema"));
      BOOST_REQUIRE(value.has_value());
      BOOST_TEST(to_string(*value) == "1");

      db.erase(meta, make_key("schema"), write_options{.sync = true});
      BOOST_TEST(!db.get(meta, make_key("schema")).has_value());

      db.put(meta, make_key("schema"), to_bytes("2"), write_options{.sync = true});
      db.flush_wal(true);
   }

   auto reopened = store{config_for(db_path)};
   const auto persisted = reopened.get(meta, make_key("schema"));
   BOOST_REQUIRE(persisted.has_value());
   BOOST_TEST(to_string(*persisted) == "2");
}

BOOST_AUTO_TEST_CASE(rocksdb_store_scan_page_bounds_prefix_with_cursor) {
   const auto root = root_guard{};
   auto db = store{config_for(root.root / "store")};
   const auto data = family{"data"};
   auto prefix = to_bytes("block:");

   for (auto value : {1U, 2U, 3U}) {
      auto key = prefix;
      forge::rocksdb::append_u64_be(key, value);
      db.put(data, key, to_bytes(std::to_string(value)));
   }
   auto other = to_bytes("other:");
   forge::rocksdb::append_u64_be(other, 1);
   db.put(data, other, to_bytes("other"));

   const auto first = db.scan_page(data, scan_request{.prefix = prefix, .limit = 2});
   BOOST_REQUIRE_EQUAL(first.entries.size(), 2U);
   BOOST_TEST(to_string(first.entries[0].value) == "1");
   BOOST_TEST(to_string(first.entries[1].value) == "2");
   BOOST_TEST(!first.next_cursor.empty());

   const auto second = db.scan_page(data, scan_request{.prefix = prefix, .cursor = first.next_cursor, .limit = 2});
   BOOST_REQUIRE_EQUAL(second.entries.size(), 1U);
   BOOST_TEST(to_string(second.entries[0].value) == "3");
   BOOST_TEST(second.next_cursor.empty());

   auto lower = prefix;
   forge::rocksdb::append_u64_be(lower, 2);
   const auto bounded = db.scan_page(data, scan_request{.prefix = prefix, .lower_bound = lower, .limit = 2});
   BOOST_REQUIRE_EQUAL(bounded.entries.size(), 2U);
   BOOST_TEST(to_string(bounded.entries[0].value) == "2");
   BOOST_TEST(to_string(bounded.entries[1].value) == "3");
}

BOOST_AUTO_TEST_CASE(rocksdb_store_batch_and_transactions_are_atomic) {
   const auto root = root_guard{};
   auto db = store{config_for(root.root / "store")};
   const auto meta = family{"meta"};
   const auto data = family{"data"};

   db.write(
      {
         operation::put(meta, make_key("schema"), to_bytes("1")),
         operation::put(data, make_u64_key(42), to_bytes("payload")),
      },
      write_options{.sync = true});

   BOOST_TEST(to_string(*db.get(meta, make_key("schema"))) == "1");
   BOOST_TEST(to_string(*db.get(data, make_u64_key(42))) == "payload");

   {
      auto transaction = db.begin(write_options{.sync = true});
      transaction.put(meta, make_key("schema"), to_bytes("3"));
      transaction.put(data, make_u64_key(7), to_bytes("rollback"));
      transaction.rollback();
   }

   BOOST_TEST(to_string(*db.get(meta, make_key("schema"))) == "1");
   BOOST_TEST(!db.get(data, make_u64_key(7)).has_value());

   auto transaction = db.begin(write_options{.sync = true});
   transaction.put(meta, make_key("schema"), to_bytes("4"));
   transaction.put(data, make_u64_key(8), to_bytes("commit"));
   transaction.commit();

   BOOST_TEST(to_string(*db.get(meta, make_key("schema"))) == "4");
   BOOST_TEST(to_string(*db.get(data, make_u64_key(8))) == "commit");
}

BOOST_AUTO_TEST_CASE(rocksdb_snapshot_preserves_old_values_and_scan_pages_after_commit) {
   const auto root = root_guard{};
   auto db = store{config_for(root.root / "store")};
   const auto data = family{"data"};
   const auto prefix = to_bytes("snap:");

   auto key_one = prefix;
   forge::rocksdb::append_u64_be(key_one, 1);
   auto key_two = prefix;
   forge::rocksdb::append_u64_be(key_two, 2);

   db.put(data, key_one, to_bytes("one"), write_options{.sync = true});

   auto snapshot = db.begin_snapshot();

   db.put(data, key_one, to_bytes("one-new"), write_options{.sync = true});
   db.put(data, key_two, to_bytes("two"), write_options{.sync = true});

   const auto old_value = snapshot.get(data, key_one);
   BOOST_REQUIRE(old_value.has_value());
   BOOST_TEST(to_string(*old_value) == "one");

   const auto snapshot_page = snapshot.scan_page(data, scan_request{.prefix = prefix, .limit = 10});
   BOOST_REQUIRE_EQUAL(snapshot_page.entries.size(), 1U);
   BOOST_TEST(to_string(snapshot_page.entries[0].value) == "one");

   const auto fresh_page = db.scan_page(data, scan_request{.prefix = prefix, .limit = 10});
   BOOST_REQUIRE_EQUAL(fresh_page.entries.size(), 2U);
   BOOST_TEST(to_string(fresh_page.entries[0].value) == "one-new");
   BOOST_TEST(to_string(fresh_page.entries[1].value) == "two");
}

BOOST_AUTO_TEST_CASE(rocksdb_transaction_move_assignment_rolls_back_replaced_transaction) {
   const auto root = root_guard{};
   auto db = store{config_for(root.root / "store")};
   const auto meta = family{"meta"};

   auto transaction = db.begin(write_options{.sync = true});
   transaction.put(meta, make_key("schema"), to_bytes("uncommitted"));

   transaction = db.begin(write_options{.sync = true});
   transaction.put(meta, make_key("schema"), to_bytes("committed"));
   transaction.commit();

   const auto value = db.get(meta, make_key("schema"));
   BOOST_REQUIRE(value.has_value());
   BOOST_TEST(to_string(*value) == "committed");
}

BOOST_AUTO_TEST_CASE(rocksdb_transaction_lock_handles_large_existing_values) {
   const auto root = root_guard{};
   auto db = store{config_for(root.root / "store")};
   const auto data = family{"data"};
   auto key = make_key("large");
   auto large_value = std::vector<std::byte>(4U * 1024U * 1024U, std::byte{0x5a});

   db.put(data, key, large_value, write_options{.sync = true});

   auto transaction = db.begin(write_options{.sync = true});
   transaction.lock(data, key);
   transaction.rollback();

   const auto persisted = db.get(data, make_key("large"));
   BOOST_REQUIRE(persisted.has_value());
   BOOST_REQUIRE_EQUAL(persisted->size(), large_value.size());
}

BOOST_AUTO_TEST_CASE(rocksdb_transaction_scans_prefix_with_local_changes_and_auto_rollback) {
   const auto root = root_guard{};
   auto db = store{config_for(root.root / "store")};
   const auto data = family{"data"};
   auto prefix = to_bytes("dirty:");

   auto key_one = prefix;
   forge::rocksdb::append_u64_be(key_one, 1);
   auto key_two = prefix;
   forge::rocksdb::append_u64_be(key_two, 2);
   auto key_three = prefix;
   forge::rocksdb::append_u64_be(key_three, 3);

   db.put(data, key_one, to_bytes("one"));
   db.put(data, key_two, to_bytes("two"));

   {
      auto transaction = db.begin(write_options{.sync = true});
      transaction.lock(data, prefix);
      transaction.erase(data, key_one);
      transaction.put(data, key_three, to_bytes("three"));

      const auto scanned = transaction.scan(data, prefix);
      BOOST_REQUIRE_EQUAL(scanned.size(), 2U);
      BOOST_TEST(read_u64_be(std::span<const std::byte>{scanned[0].key.data(), scanned[0].key.size()}.last(8)) == 2U);
      BOOST_TEST(to_string(scanned[0].value) == "two");
      BOOST_TEST(read_u64_be(std::span<const std::byte>{scanned[1].key.data(), scanned[1].key.size()}.last(8)) == 3U);
      BOOST_TEST(to_string(scanned[1].value) == "three");
   }

   const auto persisted = db.scan(data, prefix);
   BOOST_REQUIRE_EQUAL(persisted.size(), 2U);
   BOOST_TEST(read_u64_be(std::span<const std::byte>{persisted[0].key.data(), persisted[0].key.size()}.last(8)) == 1U);
   BOOST_TEST(read_u64_be(std::span<const std::byte>{persisted[1].key.data(), persisted[1].key.size()}.last(8)) == 2U);
}
