#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/objectdb/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

import forge.asio.runtime;
import forge.asio.blocking;
import forge.crypto.hex;
import forge.ids.types;
import forge.objectdb.cursor;
import forge.objectdb.descriptor;
import forge.objectdb.exceptions;
import forge.objectdb.layout;
import forge.objectdb.store;
import forge.objectdb.types;
import forge.raw.raw;

#if FORGE_HAS_ROCKSDB
import forge.rocksdb.store;
#endif

namespace objectdb_tests {

struct by_id;
struct by_name;
struct by_region_balance;
struct by_region;

struct account : forge::objectdb::object<account, 1, 7> {
   std::string name;
   std::uint64_t balance = 0;
   std::uint32_t region = 0;

   bool operator==(const account&) const = default;
};

BOOST_DESCRIBE_STRUCT(account, (forge::objectdb::object<account, 1, 7>), (name, balance, region))

std::uint32_t account_region(const account& value) {
   return value.region;
}

using account_object = forge::objectdb::object_index<
   account,
   forge::objectdb::indexed_by<
      forge::objectdb::primary_unique<by_id>,
      forge::objectdb::secondary_unique<by_name, &account::name>,
      forge::objectdb::secondary_non_unique<
         by_region_balance,
         forge::objectdb::composite_key<&account::region, &account::balance>>,
      forge::objectdb::secondary_non_unique<by_region, forge::objectdb::member_key<account_region>>>>;

struct bad_account {
   std::string name;
};

using bad_object =
   forge::objectdb::object_index<bad_account, forge::objectdb::indexed_by<forge::objectdb::primary_unique<by_id>>>;

using duplicate_tag_object =
   forge::objectdb::object_index<account,
                                 forge::objectdb::indexed_by<forge::objectdb::primary_unique<by_id>,
                                                             forge::objectdb::secondary_unique<by_id, &account::name>>>;

struct byte_less {
   bool operator()(const forge::objectdb::record_key& left, const forge::objectdb::record_key& right) const {
      return left.bytes() < right.bytes();
   }
};

struct memory_storage_state {
   std::map<forge::objectdb::record_key, std::vector<std::byte>, byte_less> records;
   std::size_t scan_calls = 0;
};

class memory_storage_session {
 public:
   explicit memory_storage_session(std::shared_ptr<memory_storage_state> state) : state_{std::move(state)} {}

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::objectdb::record_key key) {
      const auto found = state_->records.find(key);
      if (found == state_->records.end()) {
         co_return std::nullopt;
      }
      co_return found->second;
   }

   boost::asio::awaitable<void> put(forge::objectdb::record_key key, std::vector<std::byte> value) {
      state_->records[std::move(key)] = std::move(value);
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::objectdb::record_key key) {
      state_->records.erase(key);
      co_return;
   }

   boost::asio::awaitable<forge::objectdb::record_scan_result> scan_page(forge::objectdb::key_range range,
                                                                         forge::objectdb::page_request request) {
      forge::objectdb::validate_page_request(request);
      ++state_->scan_calls;

      auto result = forge::objectdb::record_scan_result{};
      auto current = state_->records.lower_bound(request.after ? request.after->boundary : range.begin);
      if (request.after && current != state_->records.end() && current->first == request.after->boundary) {
         ++current;
      }

      auto last_returned = std::optional<forge::objectdb::record_key>{};
      while (current != state_->records.end()) {
         if (range.has_end && !(current->first.bytes() < range.end.bytes())) {
            break;
         }
         result.entries.push_back(forge::objectdb::record_entry{.key = current->first, .value = current->second});
         last_returned = current->first;
         ++current;
         if (result.entries.size() == request.limit) {
            break;
         }
      }

      if (current != state_->records.end() && (!range.has_end || current->first.bytes() < range.end.bytes())) {
         result.next = std::move(last_returned);
      }

      co_return result;
   }

   boost::asio::awaitable<void> commit() {
      co_return;
   }

   boost::asio::awaitable<void> rollback() {
      co_return;
   }

 private:
   std::shared_ptr<memory_storage_state> state_;
};

class memory_storage {
 public:
   boost::asio::awaitable<memory_storage_session> session() {
      co_return memory_storage_session{state_};
   }

   [[nodiscard]] std::size_t scan_calls() const noexcept {
      return state_->scan_calls;
   }

 private:
   std::shared_ptr<memory_storage_state> state_ = std::make_shared<memory_storage_state>();
};

#if FORGE_HAS_ROCKSDB
class rocksdb_storage_session {
 public:
   explicit rocksdb_storage_session(std::shared_ptr<forge::rocksdb::store> db) : db_{std::move(db)} {}

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::objectdb::record_key key) {
      co_return db_->get(forge::rocksdb::family{"objectdb"}, key.bytes());
   }

   boost::asio::awaitable<void> put(forge::objectdb::record_key key, std::vector<std::byte> value) {
      db_->put(forge::rocksdb::family{"objectdb"}, key.bytes(), std::move(value));
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::objectdb::record_key key) {
      db_->erase(forge::rocksdb::family{"objectdb"}, key.bytes());
      co_return;
   }

   boost::asio::awaitable<forge::objectdb::record_scan_result> scan_page(forge::objectdb::key_range range,
                                                                         forge::objectdb::page_request request) {
      forge::objectdb::validate_page_request(request);
      auto prefix = range.begin.bytes();
      auto scan = db_->scan_page(
         forge::rocksdb::family{"objectdb"},
         forge::rocksdb::scan_request{
            .prefix = std::move(prefix),
            .cursor = request.after ? request.after->boundary.bytes() : std::vector<std::byte>{},
            .limit = request.limit,
         });

      auto result = forge::objectdb::record_scan_result{};
      auto last_returned = std::optional<forge::objectdb::record_key>{};
      for (auto& entry : scan.entries) {
         auto key = forge::objectdb::record_key{std::move(entry.key)};
         if (range.has_end && !(key.bytes() < range.end.bytes())) {
            break;
         }
         last_returned = key;
         result.entries.push_back(forge::objectdb::record_entry{.key = std::move(key), .value = std::move(entry.value)});
      }
      if (!scan.next_cursor.empty()) {
         result.next = std::move(last_returned);
      }
      co_return result;
   }

   boost::asio::awaitable<void> commit() {
      co_return;
   }

   boost::asio::awaitable<void> rollback() {
      co_return;
   }

 private:
   std::shared_ptr<forge::rocksdb::store> db_;
};

class rocksdb_storage {
 public:
   explicit rocksdb_storage(std::filesystem::path path)
       : db_{std::make_shared<forge::rocksdb::store>(forge::rocksdb::config{
            .path = path.string(),
            .column_families = {"objectdb"},
         })} {}

   boost::asio::awaitable<rocksdb_storage_session> session() {
      co_return rocksdb_storage_session{db_};
   }

 private:
   std::shared_ptr<forge::rocksdb::store> db_;
};
#endif

std::string hex(const std::vector<std::byte>& bytes) {
   auto out = std::ostringstream{};
   out << std::hex << std::setfill('0');
   for (auto byte : bytes) {
      out << std::setw(2) << std::to_integer<unsigned>(byte);
   }
   return out.str();
}

[[nodiscard]] account make_account(std::uint64_t instance, std::string name, std::uint64_t balance, std::uint32_t region) {
   auto value = account{};
   value.id = account::id_type{instance};
   value.name = std::move(name);
   value.balance = balance;
   value.region = region;
   return value;
}

boost::asio::awaitable<void> objectdb_store_smoke_roundtrip(auto storage) {
   auto store = forge::objectdb::store{std::move(storage)};
   store.register_object<account_object>();

   auto session = co_await store.session();
   co_await session.insert(make_account(42, "alice", 100, 3));
   co_await session.insert(make_account(43, "bob", 50, 3));
   co_await session.insert(make_account(44, "carol", 75, 4));

   const auto alice = co_await session.get(account::id_type{42});
   BOOST_CHECK_EQUAL(alice.name, "alice");

   const auto found = co_await session.template index<account_object, by_name>().find("bob");
   BOOST_REQUIRE(found.has_value());
   BOOST_CHECK_EQUAL(found->id.instance, 43U);

   const auto page = co_await session.template index<account_object, by_region_balance>()
                        .equal_range(std::make_tuple(std::uint32_t{3}))
                        .page({.limit = 100});
   BOOST_REQUIRE_EQUAL(page.items.size(), 2U);
   BOOST_CHECK_EQUAL(page.items[0].name, "bob");
   BOOST_CHECK_EQUAL(page.items[1].name, "alice");

   co_await session.erase(account::id_type{43});
   BOOST_CHECK(!(co_await session.find(account::id_type{43})).has_value());
}

} // namespace objectdb_tests

FORGE_OBJECTDB_OBJECT(objectdb_tests::account_object)

using namespace objectdb_tests;

static_assert(forge::objectdb::object_model<account_object>);
static_assert(!forge::objectdb::object_model<bad_object>);
static_assert(!forge::objectdb::object_model<duplicate_tag_object>);
static_assert(std::same_as<forge::objectdb::id_type_of<account_object>, forge::ids::typed_id<1, 7>>);
static_assert(std::same_as<forge::objectdb::object_index_for_id_t<account::id_type>, account_object>);
static_assert(std::same_as<forge::objectdb::index_by_tag<account_object, by_name>,
                           forge::objectdb::secondary_unique<by_name, &account::name>>);
static_assert(forge::objectdb::index_id_by_tag<account_object, by_id>.value == 0);
static_assert(forge::objectdb::index_id_by_tag<account_object, by_name>.value == 1);
static_assert(forge::objectdb::index_id_by_tag<account_object, by_region_balance>.value == 2);
static_assert(forge::objectdb::index_id_by_tag<account_object, by_region>.value == 3);

BOOST_AUTO_TEST_SUITE(objectdb_test_suite)

BOOST_AUTO_TEST_CASE(objectdb_base_object_raw_serializes_id_before_fields) {
   const auto value = make_account(42, "alice", 100, 3);
   const auto bytes = forge::raw::pack(value);

   BOOST_CHECK_EQUAL(forge::crypto::to_hex(bytes), "2a0000000000000005616c696365640000000000000003000000");
   BOOST_CHECK(forge::raw::unpack<account>(bytes) == value);
}

BOOST_AUTO_TEST_CASE(objectdb_descriptor_derives_type_from_base_object_id) {
   constexpr auto type = forge::objectdb::object_type_of<account_object>::value;

   BOOST_CHECK_EQUAL(type.space, 1U);
   BOOST_CHECK_EQUAL(type.type, 7U);
}

BOOST_AUTO_TEST_CASE(objectdb_layout_encodes_object_record_key_from_base_id) {
   using layout = forge::objectdb::layout<account_object>;

   const auto key = layout::object_record_key(account::id_type{42});

   BOOST_CHECK_EQUAL(hex(key.bytes()), "10010007000000000000002a");
}

BOOST_AUTO_TEST_CASE(objectdb_layout_accepts_tuple_composite_prefixes) {
   using layout = forge::objectdb::layout<account_object>;

   const auto range = layout::index_prefix<by_region_balance>(std::make_tuple(std::uint32_t{3}));

   BOOST_CHECK_EQUAL(hex(range.begin.bytes()), "210100070000000200000003");
   BOOST_REQUIRE(range.has_end);
   BOOST_CHECK_EQUAL(hex(range.end.bytes()), "210100070000000200000004");
}

BOOST_AUTO_TEST_CASE(objectdb_store_registers_objects_and_rejects_duplicate_registration) {
   auto store = forge::objectdb::store{memory_storage{}};

   BOOST_CHECK_NO_THROW(store.register_object<account_object>());
   BOOST_CHECK_THROW(store.register_object<account_object>(), forge::objectdb::exceptions::invalid_descriptor);
}

BOOST_AUTO_TEST_CASE(objectdb_session_get_find_and_erase_use_typed_id_mapping) {
   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, []() -> boost::asio::awaitable<void> {
      auto store = forge::objectdb::store{memory_storage{}};
      store.register_object<account_object>();
      auto session = co_await store.session();

      const auto value = make_account(42, "alice", 100, 3);
      co_await session.insert(value);

      BOOST_CHECK((co_await session.get(account::id_type{42})) == value);
      BOOST_REQUIRE((co_await session.find(account::id_type{42})).has_value());
      co_await session.erase(account::id_type{42});
      BOOST_CHECK(!(co_await session.find(account::id_type{42})).has_value());

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_session_runtime_object_id_requires_explicit_object_model) {
   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, []() -> boost::asio::awaitable<void> {
      auto store = forge::objectdb::store{memory_storage{}};
      store.register_object<account_object>();
      auto session = co_await store.session();

      co_await session.insert(make_account(42, "alice", 100, 3));
      const auto generic = forge::ids::object_id{.space = 1, .type = 7, .instance = 42};
      BOOST_CHECK_EQUAL((co_await session.get<account_object>(generic)).name, "alice");

      const auto wrong = forge::ids::object_id{.space = 1, .type = 8, .instance = 42};
      BOOST_CHECK_THROW((void)(co_await session.get<account_object>(wrong)),
                        forge::objectdb::exceptions::invalid_descriptor);

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_session_maintains_unique_and_non_unique_indexes) {
   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, []() -> boost::asio::awaitable<void> {
      auto store = forge::objectdb::store{memory_storage{}};
      store.register_object<account_object>();
      auto session = co_await store.session();

      co_await session.insert(make_account(42, "alice", 100, 3));
      co_await session.insert(make_account(43, "bob", 50, 3));

      const auto alice = co_await session.index<account_object, by_name>().find("alice");
      BOOST_REQUIRE(alice.has_value());
      BOOST_CHECK_EQUAL(alice->id.instance, 42U);

      const auto page = co_await session.index<account_object, by_region_balance>()
                           .equal_range(std::make_tuple(std::uint32_t{3}))
                           .page({.limit = 100});
      BOOST_REQUIRE_EQUAL(page.items.size(), 2U);
      BOOST_CHECK_EQUAL(page.items[0].name, "bob");
      BOOST_CHECK_EQUAL(page.items[1].name, "alice");

      BOOST_CHECK_THROW(co_await session.insert(make_account(44, "alice", 10, 9)),
                        forge::objectdb::exceptions::duplicate_object);

      auto replacement = make_account(42, "alice-2", 200, 5);
      co_await session.replace(replacement);
      BOOST_CHECK(!(co_await session.index<account_object, by_name>().find("alice")).has_value());
      BOOST_REQUIRE((co_await session.index<account_object, by_name>().find("alice-2")).has_value());

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_index_supports_mapper_keys_tuple_prefix_stream_and_for_each) {
   auto runtime = forge::asio::runtime{};
   auto storage = memory_storage{};
   auto* storage_ref = &storage;
   forge::asio::blocking::run(runtime, [storage]() mutable -> boost::asio::awaitable<void> {
      auto store = forge::objectdb::store{std::move(storage)};
      store.register_object<account_object>();
      auto session = co_await store.session();

      co_await session.insert(make_account(42, "alice", 100, 3));
      co_await session.insert(make_account(43, "bob", 50, 3));
      co_await session.insert(make_account(44, "carol", 75, 4));

      const auto exact = co_await session.index<account_object, by_region_balance>()
                            .equal_range(std::make_tuple(std::uint32_t{3}, std::uint64_t{100}))
                            .page({.limit = 100});
      BOOST_REQUIRE_EQUAL(exact.items.size(), 1U);
      BOOST_CHECK_EQUAL(exact.items[0].name, "alice");

      const auto mapped = co_await session.index<account_object, by_region>().equal_range(std::make_tuple(3U)).page();
      BOOST_REQUIRE_EQUAL(mapped.items.size(), 2U);

      auto stream = session.index<account_object, by_region_balance>()
                       .equal_range(std::make_tuple(std::uint32_t{3}))
                       .stream({.page_size = 1});
      BOOST_REQUIRE((co_await stream.next()).has_value());
      BOOST_REQUIRE((co_await stream.next()).has_value());
      BOOST_CHECK(!(co_await stream.next()).has_value());

      auto visited = std::vector<std::string>{};
      co_await session.index<account_object, by_region_balance>()
         .equal_range(std::make_tuple(std::uint32_t{3}))
         .for_each({.page_size = 1}, [&visited](const account& value) -> boost::asio::awaitable<void> {
            visited.push_back(value.name);
            co_return;
         });
      BOOST_REQUIRE_EQUAL(visited.size(), 2U);
      BOOST_CHECK_EQUAL(visited[0], "bob");
      BOOST_CHECK_EQUAL(visited[1], "alice");

      co_return;
   }());

   BOOST_CHECK_GE(storage_ref->scan_calls(), 2U);
}

BOOST_AUTO_TEST_CASE(objectdb_memory_storage_context_roundtrips_object_and_index_records) {
   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, objectdb_store_smoke_roundtrip(memory_storage{}));
}

#if FORGE_HAS_ROCKSDB
BOOST_AUTO_TEST_CASE(objectdb_rocksdb_storage_context_persists_object_and_index_records) {
   const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
   const auto root = std::filesystem::temp_directory_path() / ("forge_objectdb_rocksdb_" + std::to_string(nonce));
   std::filesystem::remove_all(root);

   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, objectdb_store_smoke_roundtrip(rocksdb_storage{root / "store"}));
   forge::asio::blocking::run(runtime, [] (std::filesystem::path path) -> boost::asio::awaitable<void> {
      auto store = forge::objectdb::store{rocksdb_storage{std::move(path)}};
      store.register_object<account_object>();
      auto session = co_await store.session();
      BOOST_CHECK_EQUAL((co_await session.get(account::id_type{42})).name, "alice");
      BOOST_REQUIRE((co_await session.index<account_object, by_name>().find("alice")).has_value());
      co_return;
   }(root / "store"));

   std::filesystem::remove_all(root);
}
#endif

BOOST_AUTO_TEST_CASE(objectdb_cursor_is_opaque_key_boundary) {
   using layout = forge::objectdb::layout<account_object>;

   const auto key = layout::object_record_key(account::id_type{42});
   const auto cursor = forge::objectdb::cursor{.boundary = key};

   BOOST_CHECK(cursor.boundary == key);
}

BOOST_AUTO_TEST_CASE(objectdb_page_request_rejects_invalid_limits_with_typed_exception) {
   BOOST_CHECK_THROW(forge::objectdb::validate_page_request(forge::objectdb::page_request{.limit = 0}),
                     forge::objectdb::exceptions::invalid_cursor);
   BOOST_CHECK_THROW(forge::objectdb::validate_page_request(
                        forge::objectdb::page_request{.limit = forge::objectdb::max_page_limit + 1}),
                     forge::objectdb::exceptions::invalid_cursor);
   BOOST_CHECK_NO_THROW(forge::objectdb::validate_page_request(forge::objectdb::page_request{.limit = 100}));
}

BOOST_AUTO_TEST_SUITE_END()
