#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/exceptions/macros.hpp>
#include <forge/objectdb/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <map>
#include <memory>
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
import forge.objectdb.exceptions;
import forge.objectdb.hooks;
import forge.objectdb.index;
import forge.objectdb.object;
import forge.objectdb.record;
import forge.objectdb.session;
import forge.objectdb.store;
import forge.raw.raw;

#if FORGE_HAS_ROCKSDB
import forge.objectdb.rocksdb;
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

struct memory_state {
   std::map<forge::objectdb::record_key, std::vector<std::byte>, byte_less> records;
   std::size_t scan_calls = 0;
   std::size_t active_writes = 0;
   bool overlapping_writes = false;
};

class memory_session final : public forge::objectdb::session {
 public:
   explicit memory_session(std::shared_ptr<memory_state> state) : state_{std::move(state)}, working_{state_->records} {
      ++state_->active_writes;
      if (state_->active_writes > 1) {
         state_->overlapping_writes = true;
      }
   }

   ~memory_session() override {
      finish();
   }

   [[nodiscard]] forge::objectdb::capabilities capabilities() const noexcept override {
      return forge::objectdb::capabilities{.snapshot_reads = false, .writes = true};
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::objectdb::record_key key) override {
      const auto found = working_.find(key);
      if (found == working_.end()) {
         co_return std::nullopt;
      }
      co_return found->second;
   }

   boost::asio::awaitable<void> put(forge::objectdb::record_key key, std::vector<std::byte> value) override {
      working_[std::move(key)] = std::move(value);
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::objectdb::record_key key) override {
      working_.erase(key);
      co_return;
   }

   boost::asio::awaitable<forge::objectdb::record_page> scan_page(forge::objectdb::record_range range,
                                                                  forge::objectdb::page_request request) override {
      forge::objectdb::validate_page_request(request);
      ++state_->scan_calls;

      auto result = forge::objectdb::record_page{};
      auto current = working_.lower_bound(request.after ? request.after->boundary : range.begin);
      if (request.after && current != working_.end() && current->first == request.after->boundary) {
         ++current;
      }

      auto last_returned = std::optional<forge::objectdb::record_key>{};
      while (current != working_.end()) {
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

      if (current != working_.end() && (!range.has_end || current->first.bytes() < range.end.bytes())) {
         result.next = std::move(last_returned);
      }

      co_return result;
   }

   boost::asio::awaitable<void> commit() override {
      state_->records = std::move(working_);
      finish();
      co_return;
   }

   boost::asio::awaitable<void> rollback() override {
      finish();
      working_.clear();
      co_return;
   }

   [[nodiscard]] bool closed() const noexcept {
      return closed_;
   }

 private:
   void finish() noexcept {
      if (!closed_) {
         closed_ = true;
         --state_->active_writes;
      }
   }

   std::shared_ptr<memory_state> state_;
   std::map<forge::objectdb::record_key, std::vector<std::byte>, byte_less> working_;
   bool closed_ = false;
};

class memory_snapshot_session final : public forge::objectdb::session {
 public:
   explicit memory_snapshot_session(std::shared_ptr<memory_state> state)
       : state_{std::move(state)}, snapshot_{state_->records} {}

   [[nodiscard]] forge::objectdb::capabilities capabilities() const noexcept override {
      return forge::objectdb::capabilities{.snapshot_reads = true, .writes = false};
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::objectdb::record_key key) override {
      const auto found = snapshot_.find(key);
      if (found == snapshot_.end()) {
         co_return std::nullopt;
      }
      co_return found->second;
   }

   boost::asio::awaitable<void> put(forge::objectdb::record_key, std::vector<std::byte>) override {
      FORGE_THROW_EXCEPTION(forge::objectdb::exceptions::unsupported_operation, "memory snapshot is read-only");
   }

   boost::asio::awaitable<void> erase(forge::objectdb::record_key) override {
      FORGE_THROW_EXCEPTION(forge::objectdb::exceptions::unsupported_operation, "memory snapshot is read-only");
   }

   boost::asio::awaitable<forge::objectdb::record_page> scan_page(forge::objectdb::record_range range,
                                                                  forge::objectdb::page_request request) override {
      forge::objectdb::validate_page_request(request);
      ++state_->scan_calls;

      auto result = forge::objectdb::record_page{};
      auto current = snapshot_.lower_bound(request.after ? request.after->boundary : range.begin);
      if (request.after && current != snapshot_.end() && current->first == request.after->boundary) {
         ++current;
      }

      auto last_returned = std::optional<forge::objectdb::record_key>{};
      while (current != snapshot_.end()) {
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

      if (current != snapshot_.end() && (!range.has_end || current->first.bytes() < range.end.bytes())) {
         result.next = std::move(last_returned);
      }

      co_return result;
   }

   boost::asio::awaitable<void> commit() override {
      FORGE_THROW_EXCEPTION(forge::objectdb::exceptions::unsupported_operation, "memory snapshot cannot commit");
   }

   boost::asio::awaitable<void> rollback() override {
      co_return;
   }

 private:
   std::shared_ptr<memory_state> state_;
   std::map<forge::objectdb::record_key, std::vector<std::byte>, byte_less> snapshot_;
};

class invalid_session final : public forge::objectdb::session {
 public:
   [[nodiscard]] forge::objectdb::capabilities capabilities() const noexcept override {
      return forge::objectdb::capabilities{.snapshot_reads = false, .writes = false};
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::objectdb::record_key) override {
      co_return std::nullopt;
   }

   boost::asio::awaitable<void> put(forge::objectdb::record_key, std::vector<std::byte>) override {
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::objectdb::record_key) override {
      co_return;
   }

   boost::asio::awaitable<forge::objectdb::record_page> scan_page(forge::objectdb::record_range,
                                                                  forge::objectdb::page_request) override {
      co_return forge::objectdb::record_page{};
   }

   boost::asio::awaitable<void> commit() override {
      co_return;
   }

   boost::asio::awaitable<void> rollback() override {
      co_return;
   }
};

class memory_driver {
 public:
   [[nodiscard]] forge::objectdb::session_factory<memory_session> session_factory() const {
      return forge::objectdb::session_factory<memory_session>{
         [state = state_]() -> boost::asio::awaitable<std::unique_ptr<memory_session>> {
            co_return std::make_unique<memory_session>(state);
         }};
   }

   [[nodiscard]] forge::objectdb::session_factory<memory_snapshot_session> snapshot_factory() const {
      return forge::objectdb::session_factory<memory_snapshot_session>{
         [state = state_]() -> boost::asio::awaitable<std::unique_ptr<memory_snapshot_session>> {
            co_return std::make_unique<memory_snapshot_session>(state);
         }};
   }

   [[nodiscard]] std::size_t scan_calls() const noexcept {
      return state_->scan_calls;
   }

   [[nodiscard]] std::size_t record_count() const noexcept {
      return state_->records.size();
   }

   [[nodiscard]] std::vector<forge::objectdb::record_key> keys() const {
      auto out = std::vector<forge::objectdb::record_key>{};
      out.reserve(state_->records.size());
      for (const auto& [key, _] : state_->records) {
         out.push_back(key);
      }
      return out;
   }

   [[nodiscard]] bool overlapping_writes() const noexcept {
      return state_->overlapping_writes;
   }

 private:
   std::shared_ptr<memory_state> state_ = std::make_shared<memory_state>();
};

class veto_interceptor final : public forge::objectdb::interceptor {
 public:
   boost::asio::awaitable<void> before_mutation(const forge::objectdb::object_mutation& mutation) override {
      ++calls;
      last = mutation.kind;
      FORGE_THROW_EXCEPTION(forge::objectdb::exceptions::duplicate_object, "objectdb test interceptor veto");
   }

   std::size_t calls = 0;
   std::optional<forge::objectdb::mutation_kind> last;
};

class counting_observer final : public forge::objectdb::observer {
 public:
   boost::asio::awaitable<void> after_commit(const forge::objectdb::change_set& changes) override {
      ++calls;
      mutation_count += changes.mutations.size();
      last = changes;
      co_return;
   }

   std::size_t calls = 0;
   std::size_t mutation_count = 0;
   std::optional<forge::objectdb::change_set> last;
};

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

[[nodiscard]] forge::objectdb::store make_store(const memory_driver& driver) {
   auto store = forge::objectdb::store{driver.session_factory(), driver.snapshot_factory()};
   store.register_object<account_object>();
   return store;
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
static_assert(forge::objectdb::index_id_by_tag<account_object, by_id> == 0);
static_assert(forge::objectdb::index_id_by_tag<account_object, by_name> == 1);
static_assert(forge::objectdb::index_id_by_tag<account_object, by_region_balance> == 2);
static_assert(forge::objectdb::index_id_by_tag<account_object, by_region> == 3);

BOOST_AUTO_TEST_SUITE(objectdb_test_suite)

BOOST_AUTO_TEST_CASE(objectdb_base_object_raw_serializes_id_before_fields) {
   const auto value = make_account(42, "alice", 100, 3);
   const auto bytes = forge::raw::pack(value);

   BOOST_CHECK_EQUAL(forge::crypto::to_hex(bytes), "2a0000000000000005616c696365640000000000000003000000");
   BOOST_CHECK(forge::raw::unpack<account>(bytes) == value);
}

BOOST_AUTO_TEST_CASE(objectdb_descriptor_derives_type_from_base_object_id) {
   constexpr auto type = forge::objectdb::object_id_of<account_object>::value;

   BOOST_CHECK_EQUAL(type.space, 1U);
   BOOST_CHECK_EQUAL(type.type, 7U);
}

BOOST_AUTO_TEST_CASE(objectdb_store_materializes_object_record_key_from_base_id) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);
      co_await store.insert(make_account(42, "alice", 100, 3));
      co_return;
   }());

   const auto keys = driver.keys();
   BOOST_REQUIRE(!keys.empty());
   BOOST_CHECK_EQUAL(hex(keys.front().bytes()), "10010007000000000000002a");
}

BOOST_AUTO_TEST_CASE(objectdb_store_materializes_tuple_composite_index_key) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);
      co_await store.insert(make_account(42, "alice", 100, 3));
      co_return;
   }());

   const auto keys = driver.keys();
   BOOST_REQUIRE_EQUAL(keys.size(), 4U);
   BOOST_CHECK_EQUAL(hex(keys[2].bytes()), "2101000700000002000000030000000000000064000000000000002a");
}

BOOST_AUTO_TEST_CASE(objectdb_store_registers_objects_and_rejects_duplicate_registration) {
   auto store = forge::objectdb::store{memory_driver{}.session_factory()};

   BOOST_CHECK_NO_THROW(store.register_object<account_object>());
   BOOST_CHECK_THROW(store.register_object<account_object>(), forge::objectdb::exceptions::invalid_descriptor);
}

BOOST_AUTO_TEST_CASE(objectdb_store_direct_api_autocommits_and_reads_indexes) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);

      co_await store.insert(make_account(42, "alice", 100, 3));
      co_await store.insert(make_account(43, "bob", 50, 3));
      co_await store.insert(make_account(44, "carol", 75, 4));

      const auto alice = co_await store.get(account::id_type{42});
      BOOST_CHECK_EQUAL(alice.name, "alice");

      const auto found = co_await store.index<account_object, by_name>().find("bob");
      BOOST_REQUIRE(found.has_value());
      BOOST_CHECK_EQUAL(found->id.instance, 43U);

      const auto page = co_await store.index<account_object, by_region_balance>()
                           .equal_range(std::make_tuple(std::uint32_t{3}))
                           .page({.limit = 100});
      BOOST_REQUIRE_EQUAL(page.items.size(), 2U);
      BOOST_CHECK_EQUAL(page.items[0].name, "bob");
      BOOST_CHECK_EQUAL(page.items[1].name, "alice");

      co_await store.erase(account::id_type{43});
      BOOST_CHECK(!(co_await store.find(account::id_type{43})).has_value());

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_transaction_groups_mutations_and_requires_commit) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);

      auto tx = co_await store.begin_transaction();
      co_await tx.insert(make_account(42, "alice", 100, 3));
      BOOST_CHECK(!(co_await store.find(account::id_type{42})).has_value());
      co_await tx.commit();
      BOOST_CHECK_EQUAL((co_await store.get(account::id_type{42})).name, "alice");

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_transaction_destruction_discards_uncommitted_changes) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);

      {
         auto tx = co_await store.begin_transaction();
         co_await tx.insert(make_account(42, "alice", 100, 3));
      }

      BOOST_CHECK(!(co_await store.find(account::id_type{42})).has_value());
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_begin_read_requires_snapshot_capability) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = forge::objectdb::store{driver.session_factory()};
      store.register_object<account_object>();

      BOOST_CHECK_THROW((void)(co_await store.begin_read()), forge::objectdb::exceptions::unsupported_operation);

      auto invalid = forge::objectdb::session_factory<invalid_session>{
         []() -> boost::asio::awaitable<std::unique_ptr<invalid_session>> {
            co_return std::make_unique<invalid_session>();
         }};
      auto invalid_store = forge::objectdb::store{invalid};
      invalid_store.register_object<account_object>();
      BOOST_CHECK_THROW((void)(co_await invalid_store.begin_transaction()),
                        forge::objectdb::exceptions::unsupported_operation);

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_memory_snapshot_preserves_old_state_across_writes) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);

      co_await store.insert(make_account(42, "alice", 100, 3));

      auto snapshot = co_await store.begin_read();
      co_await store.modify(account::id_type{42}, [](account& value) {
         value.balance = 200;
         value.name = "alice-new";
      });

      const auto old_value = co_await snapshot.get(account::id_type{42});
      BOOST_CHECK_EQUAL(old_value.name, "alice");
      BOOST_CHECK_EQUAL(old_value.balance, 100U);

      const auto new_value = co_await store.get(account::id_type{42});
      BOOST_CHECK_EQUAL(new_value.name, "alice-new");
      BOOST_CHECK_EQUAL(new_value.balance, 200U);

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_store_stream_uses_one_snapshot_across_pages) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);

      co_await store.insert(make_account(42, "alice", 100, 3));
      co_await store.insert(make_account(43, "bob", 50, 3));

      auto stream = store.index<account_object, by_region_balance>()
                       .equal_range(std::make_tuple(std::uint32_t{3}))
                       .stream({.page_size = 1});

      const auto first = co_await stream.next();
      BOOST_REQUIRE(first.has_value());
      BOOST_CHECK_EQUAL(first->name, "bob");

      co_await store.insert(make_account(44, "carol", 75, 3));

      const auto second = co_await stream.next();
      BOOST_REQUIRE(second.has_value());
      BOOST_CHECK_EQUAL(second->name, "alice");
      BOOST_CHECK(!(co_await stream.next()).has_value());

      const auto fresh = co_await store.index<account_object, by_region_balance>()
                            .equal_range(std::make_tuple(std::uint32_t{3}))
                            .page({.limit = 10});
      BOOST_REQUIRE_EQUAL(fresh.items.size(), 3U);

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_single_writer_serializes_concurrent_transactions) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);
      auto first = co_await store.begin_transaction();

      auto second_started = std::make_shared<bool>(false);
      auto second_error = std::make_shared<std::exception_ptr>();
      const auto executor = co_await boost::asio::this_coro::executor;
      boost::asio::co_spawn(
         executor,
         [store, second_started, second_error]() mutable -> boost::asio::awaitable<void> {
            try {
               auto second = co_await store.begin_transaction();
               *second_started = true;
               co_await second.rollback();
            } catch (...) {
               *second_error = std::current_exception();
            }
         },
         boost::asio::detached);

      auto timer = boost::asio::steady_timer{executor};
      timer.expires_after(std::chrono::milliseconds{50});
      co_await timer.async_wait(boost::asio::use_awaitable);

      BOOST_CHECK(!*second_started);
      BOOST_CHECK(!driver.overlapping_writes());

      co_await first.rollback();

      timer.expires_after(std::chrono::milliseconds{50});
      co_await timer.async_wait(boost::asio::use_awaitable);

      if (*second_error) {
         std::rethrow_exception(*second_error);
      }
      BOOST_CHECK(*second_started);
      BOOST_CHECK(!driver.overlapping_writes());

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_session_runtime_object_id_requires_explicit_object_model) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);

      co_await store.insert(make_account(42, "alice", 100, 3));
      const auto generic = forge::ids::object_id{.space = 1, .type = 7, .instance = 42};
      BOOST_CHECK_EQUAL((co_await store.get<account_object>(generic)).name, "alice");

      const auto wrong = forge::ids::object_id{.space = 1, .type = 8, .instance = 42};
      BOOST_CHECK_THROW((void)(co_await store.get<account_object>(wrong)),
                        forge::objectdb::exceptions::invalid_descriptor);

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_modify_updates_secondary_indexes_and_unique_constraints) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);

      co_await store.insert(make_account(42, "alice", 100, 3));
      co_await store.insert(make_account(43, "bob", 50, 3));

      co_await store.modify(account::id_type{42}, [](account& value) {
         value.name = "alice-2";
         value.balance = 200;
         value.region = 5;
      });

      BOOST_CHECK(!(co_await store.index<account_object, by_name>().find("alice")).has_value());
      BOOST_REQUIRE((co_await store.index<account_object, by_name>().find("alice-2")).has_value());

      BOOST_CHECK_THROW(co_await store.modify(account::id_type{43}, [](account& value) { value.name = "alice-2"; }),
                        forge::objectdb::exceptions::duplicate_object);
      BOOST_CHECK_EQUAL((co_await store.get(account::id_type{43})).name, "bob");

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_index_supports_mapper_keys_tuple_prefix_stream_and_for_each) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);

      co_await store.insert(make_account(42, "alice", 100, 3));
      co_await store.insert(make_account(43, "bob", 50, 3));
      co_await store.insert(make_account(44, "carol", 75, 4));

      const auto exact = co_await store.index<account_object, by_region_balance>()
                            .equal_range(std::make_tuple(std::uint32_t{3}, std::uint64_t{100}))
                            .page({.limit = 100});
      BOOST_REQUIRE_EQUAL(exact.items.size(), 1U);
      BOOST_CHECK_EQUAL(exact.items[0].name, "alice");

      const auto mapped = co_await store.index<account_object, by_region>().equal_range(std::make_tuple(3U)).page();
      BOOST_REQUIRE_EQUAL(mapped.items.size(), 2U);

      auto stream = store.index<account_object, by_region_balance>()
                       .equal_range(std::make_tuple(std::uint32_t{3}))
                       .stream({.page_size = 1});
      BOOST_REQUIRE((co_await stream.next()).has_value());
      BOOST_REQUIRE((co_await stream.next()).has_value());
      BOOST_CHECK(!(co_await stream.next()).has_value());

      auto visited = std::vector<std::string>{};
      co_await store.index<account_object, by_region_balance>()
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

   BOOST_CHECK_GE(driver.scan_calls(), 2U);
}

BOOST_AUTO_TEST_CASE(objectdb_direct_mutation_rolls_back_when_interceptor_vetoes) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);
      auto veto = std::make_shared<veto_interceptor>();
      store.add_interceptor(veto);

      BOOST_CHECK_THROW(co_await store.insert(make_account(42, "alice", 100, 3)),
                        forge::objectdb::exceptions::duplicate_object);
      BOOST_CHECK_EQUAL(veto->calls, 1U);
      BOOST_CHECK(!(co_await store.find(account::id_type{42})).has_value());
      BOOST_CHECK_EQUAL(driver.record_count(), 0U);

      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(objectdb_observer_runs_after_commit_only) {
   auto runtime = forge::asio::runtime{};
   auto driver = memory_driver{};
   forge::asio::blocking::run(runtime, [&driver]() -> boost::asio::awaitable<void> {
      auto store = make_store(driver);
      auto observer = std::make_shared<counting_observer>();
      store.add_observer(observer);

      {
         auto tx = co_await store.begin_transaction();
         co_await tx.insert(make_account(42, "alice", 100, 3));
         co_await tx.rollback();
      }
      BOOST_CHECK_EQUAL(observer->calls, 0U);

      co_await store.insert(make_account(43, "bob", 50, 3));
      BOOST_CHECK_EQUAL(observer->calls, 1U);
      BOOST_CHECK_EQUAL(observer->mutation_count, 1U);
      BOOST_REQUIRE(observer->last.has_value());
      BOOST_CHECK_EQUAL(static_cast<int>(observer->last->mutations.front().kind),
                        static_cast<int>(forge::objectdb::mutation_kind::insert));

      co_return;
   }());
}

#if FORGE_HAS_ROCKSDB
BOOST_AUTO_TEST_CASE(objectdb_rocksdb_driver_persists_objects_indexes_pages_and_streams) {
   const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
   const auto root = std::filesystem::temp_directory_path() / ("forge_objectdb_driver_" + std::to_string(nonce));
   std::filesystem::remove_all(root);

   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, [root]() -> boost::asio::awaitable<void> {
      auto driver = forge::objectdb::rocksdb::driver{forge::objectdb::rocksdb::config{.path = (root / "store").string()}};
      auto store = forge::objectdb::store{driver.session_factory(), driver.snapshot_factory()};
      store.register_object<account_object>();

      auto tx = co_await store.begin_transaction();
      co_await tx.insert(make_account(42, "alice", 100, 3));
      co_await tx.insert(make_account(43, "bob", 50, 3));
      co_await tx.commit();
      driver.flush();

      co_return;
   }());

   forge::asio::blocking::run(runtime, [root]() -> boost::asio::awaitable<void> {
      auto driver = forge::objectdb::rocksdb::driver{forge::objectdb::rocksdb::config{.path = (root / "store").string()}};
      auto store = forge::objectdb::store{driver.session_factory(), driver.snapshot_factory()};
      store.register_object<account_object>();

      BOOST_CHECK_EQUAL((co_await store.get(account::id_type{42})).name, "alice");

      const auto page = co_await store.index<account_object, by_region_balance>()
                           .equal_range(std::make_tuple(std::uint32_t{3}))
                           .page({.limit = 1});
      BOOST_REQUIRE_EQUAL(page.items.size(), 1U);
      BOOST_CHECK_EQUAL(page.items[0].name, "bob");
      BOOST_REQUIRE(page.next.has_value());

      auto stream = store.index<account_object, by_region_balance>()
                       .equal_range(std::make_tuple(std::uint32_t{3}))
                       .stream({.page_size = 1});
      BOOST_REQUIRE((co_await stream.next()).has_value());
      BOOST_REQUIRE((co_await stream.next()).has_value());
      BOOST_CHECK(!(co_await stream.next()).has_value());

      const auto lower = co_await store.index<account_object, by_region_balance>()
                            .lower_bound(std::make_tuple(std::uint32_t{3}, std::uint64_t{100}))
                            .page({.limit = 2});
      BOOST_REQUIRE_EQUAL(lower.items.size(), 1U);
      BOOST_CHECK_EQUAL(lower.items[0].name, "alice");

      co_return;
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(objectdb_rocksdb_driver_rolls_back_uncommitted_transaction) {
   const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
   const auto root = std::filesystem::temp_directory_path() / ("forge_objectdb_rollback_" + std::to_string(nonce));
   std::filesystem::remove_all(root);

   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, [root]() -> boost::asio::awaitable<void> {
      auto driver = forge::objectdb::rocksdb::driver{forge::objectdb::rocksdb::config{.path = (root / "store").string()}};
      auto store = forge::objectdb::store{driver.session_factory(), driver.snapshot_factory()};
      store.register_object<account_object>();

      {
         auto tx = co_await store.begin_transaction();
         co_await tx.insert(make_account(42, "alice", 100, 3));
      }

      BOOST_CHECK(!(co_await store.find(account::id_type{42})).has_value());
      co_return;
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(objectdb_rocksdb_snapshot_preserves_old_state_across_writes) {
   const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
   const auto root = std::filesystem::temp_directory_path() / ("forge_objectdb_snapshot_" + std::to_string(nonce));
   std::filesystem::remove_all(root);

   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, [root]() -> boost::asio::awaitable<void> {
      auto driver = forge::objectdb::rocksdb::driver{forge::objectdb::rocksdb::config{.path = (root / "store").string()}};
      auto store = forge::objectdb::store{driver.session_factory(), driver.snapshot_factory()};
      store.register_object<account_object>();

      co_await store.insert(make_account(42, "alice", 100, 3));
      auto snapshot = co_await store.begin_read();
      co_await store.modify(account::id_type{42}, [](account& value) {
         value.name = "alice-new";
         value.balance = 200;
      });

      BOOST_CHECK_EQUAL((co_await snapshot.get(account::id_type{42})).name, "alice");
      BOOST_CHECK_EQUAL((co_await store.get(account::id_type{42})).name, "alice-new");

      co_return;
   }());

   std::filesystem::remove_all(root);
}
#endif

BOOST_AUTO_TEST_CASE(objectdb_cursor_is_opaque_key_boundary) {
   const auto key = forge::objectdb::record_key{
      std::vector<std::byte>{std::byte{0x10}, std::byte{0x01}, std::byte{0x00}, std::byte{0x07}}};
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
