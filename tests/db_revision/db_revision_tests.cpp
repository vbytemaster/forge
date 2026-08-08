#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/system/error_code.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/db/object/macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.blob.store;
import forge.db.blob.types;
import forge.db.core.driver;
import forge.db.core.exceptions;
import forge.db.core.record;
import forge.db.object.store;
import forge.db.object.system;
import forge.db.object.transaction;
import forge.db.object.hooks;
import forge.db.object.index;
import forge.db.object.object;
import forge.db.revision.exceptions;
import forge.db.revision.store;
import forge.db.revision.types;
import forge.db.ids.object_id;

namespace {

using record_map = std::map<forge::db::core::record_key, std::vector<std::byte>>;
using family_map = std::map<std::string, record_map>;

std::vector<std::byte> bytes(std::string value) {
   return {
       reinterpret_cast<const std::byte*>(value.data()),
       reinterpret_cast<const std::byte*>(value.data() + value.size()),
   };
}

std::string text(const std::vector<std::byte>& value) {
   return {
       reinterpret_cast<const char*>(value.data()),
       reinterpret_cast<const char*>(value.data() + value.size()),
   };
}

forge::db::core::record_key key(std::string value) {
   return forge::db::core::record_key{bytes(std::move(value))};
}

bool starts_with(const forge::db::core::record_key& value, const forge::db::core::record_key& prefix) {
   return prefix.empty() || (value.bytes().size() >= prefix.bytes().size() &&
                             std::equal(prefix.bytes().begin(), prefix.bytes().end(), value.bytes().begin()));
}

struct memory_state {
   struct lock_waiter {
      explicit lock_waiter(boost::asio::any_io_executor executor)
          : timer{std::move(executor), boost::asio::steady_timer::time_point::max()} {}

      boost::asio::steady_timer timer;
      bool granted = false;
   };

   mutable std::mutex mutex;
   family_map records;
   bool record_locks = true;
   bool savepoints = true;
   bool record_locked = false;
   std::deque<std::shared_ptr<lock_waiter>> lock_waiters;
};

class memory_session final : public forge::db::core::session {
 public:
   memory_session(std::shared_ptr<memory_state> state, bool snapshot, bool writable)
       : state_{std::move(state)}, snapshot_{snapshot}, writable_{writable} {
      auto lock = std::scoped_lock{state_->mutex};
      working_ = state_->records;
   }

   ~memory_session() override {
      release_record_lock();
   }

   [[nodiscard]] forge::db::core::capabilities capabilities() const noexcept override {
      return forge::db::core::capabilities{
          .snapshot_reads = snapshot_,
          .writes = writable_,
          .savepoints = writable_ && state_->savepoints,
          .record_locks = writable_ && state_->record_locks,
      };
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::db::core::family family,
                                                                     forge::db::core::record_key record) override {
      const auto family_found = working_.find(family.name);
      if (family_found == working_.end()) {
         co_return std::nullopt;
      }
      const auto found = family_found->second.find(record);
      if (found == family_found->second.end()) {
         co_return std::nullopt;
      }
      co_return found->second;
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get_for_update(forge::db::core::family family, forge::db::core::record_key record) override {
      if (!record_lock_owned_) {
         const auto executor = co_await boost::asio::this_coro::executor;
         auto waiter = std::make_shared<memory_state::lock_waiter>(executor);
         {
            auto lock = std::scoped_lock{state_->mutex};
            if (!state_->record_locked) {
               state_->record_locked = true;
               record_lock_owned_ = true;
               working_ = state_->records;
            } else {
               state_->lock_waiters.push_back(waiter);
            }
         }

         if (!record_lock_owned_) {
            auto error = boost::system::error_code{};
            co_await waiter->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
            {
               auto lock = std::scoped_lock{state_->mutex};
               if (!waiter->granted) {
                  throw boost::system::system_error{error ? error : boost::asio::error::operation_aborted};
               }
               record_lock_owned_ = true;
               working_ = state_->records;
            }
         }
      }
      co_return co_await get(std::move(family), std::move(record));
   }

   boost::asio::awaitable<void> put(forge::db::core::family family, forge::db::core::record_key record,
                                    std::vector<std::byte> value) override {
      working_[family.name][std::move(record)] = std::move(value);
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::db::core::family family, forge::db::core::record_key record) override {
      working_[family.name].erase(record);
      co_return;
   }

   boost::asio::awaitable<forge::db::core::record_page> scan_page(forge::db::core::family family,
                                                                  forge::db::core::record_range range,
                                                                  forge::db::core::page_request request) override {
      forge::db::core::validate_page_request(request);
      auto result = forge::db::core::record_page{};
      const auto family_found = working_.find(family.name);
      if (family_found == working_.end()) {
         co_return result;
      }

      auto current = family_found->second.lower_bound(request.after ? request.after->boundary : range.begin);
      if (request.after && current != family_found->second.end() && current->first == request.after->boundary) {
         ++current;
      }

      auto last = std::optional<forge::db::core::record_key>{};
      while (current != family_found->second.end()) {
         if (!starts_with(current->first, range.prefix) ||
             (range.has_end && !(current->first.bytes() < range.end.bytes()))) {
            break;
         }
         result.entries.push_back({.key = current->first, .value = current->second});
         last = current->first;
         ++current;
         if (result.entries.size() == request.limit) {
            break;
         }
      }
      if (last && current != family_found->second.end() && starts_with(current->first, range.prefix) &&
          (!range.has_end || current->first.bytes() < range.end.bytes())) {
         result.next = forge::db::core::cursor{.boundary = *last};
      }
      co_return result;
   }

   boost::asio::awaitable<void> create_savepoint() override {
      savepoints_.push_back(working_);
      co_return;
   }

   boost::asio::awaitable<void> rollback_to_savepoint() override {
      if (savepoints_.empty()) {
         throw std::logic_error{"db revision test savepoint stack is empty"};
      }
      working_ = std::move(savepoints_.back());
      savepoints_.pop_back();
      co_return;
   }

   boost::asio::awaitable<void> release_savepoint() override {
      if (savepoints_.empty()) {
         throw std::logic_error{"db revision test savepoint stack is empty"};
      }
      savepoints_.pop_back();
      co_return;
   }

   boost::asio::awaitable<void> commit() override {
      if (writable_) {
         auto lock = std::scoped_lock{state_->mutex};
         state_->records = std::move(working_);
      }
      release_record_lock();
      co_return;
   }

   boost::asio::awaitable<void> rollback() override {
      release_record_lock();
      co_return;
   }

 private:
   void release_record_lock() noexcept {
      if (!record_lock_owned_) {
         return;
      }
      record_lock_owned_ = false;

      auto next = std::shared_ptr<memory_state::lock_waiter>{};
      {
         auto lock = std::scoped_lock{state_->mutex};
         if (!state_->lock_waiters.empty()) {
            next = std::move(state_->lock_waiters.front());
            state_->lock_waiters.pop_front();
            next->granted = true;
         } else {
            state_->record_locked = false;
         }
      }
      if (next) {
         boost::asio::dispatch(next->timer.get_executor(), [next] {
            try {
               next->timer.cancel();
            } catch (...) {
            }
         });
      }
   }

   std::shared_ptr<memory_state> state_;
   family_map working_;
   std::vector<family_map> savepoints_;
   bool snapshot_ = false;
   bool writable_ = false;
   bool record_lock_owned_ = false;
};

class memory_driver final : public forge::db::core::driver {
 public:
   explicit memory_driver(std::shared_ptr<memory_state> state) : state_{std::move(state)} {}

   boost::asio::awaitable<void> async_flush(bool) override {
      co_return;
   }

 private:
   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>> open_transaction() override {
      co_return std::make_unique<memory_session>(state_, false, true);
   }

   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>> open_snapshot() override {
      co_return std::make_unique<memory_session>(state_, true, false);
   }

   std::shared_ptr<memory_state> state_;
};

struct environment {
   std::shared_ptr<memory_driver> driver;
   forge::db::object::store objects;
   forge::db::revision::store revisions;
};

boost::asio::awaitable<environment>
open_environment(std::shared_ptr<memory_state> state = std::make_shared<memory_state>(),
                 forge::db::object::store::options object_options = {}) {
   auto driver = std::make_shared<memory_driver>(std::move(state));
   auto objects = co_await forge::db::object::store::open(driver, object_options);
   auto revisions = co_await forge::db::revision::store::open(driver, objects);
   co_return environment{
       .driver = std::move(driver),
       .objects = std::move(objects),
       .revisions = std::move(revisions),
   };
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> read_record(const std::shared_ptr<memory_driver>& driver,
                                                                          forge::db::core::family family,
                                                                          forge::db::core::record_key record) {
   auto read = co_await driver->begin_read();
   co_return co_await read.get(std::move(family), std::move(record));
}

boost::asio::awaitable<void> seed_record(const std::shared_ptr<memory_driver>& driver, forge::db::core::family family,
                                         forge::db::core::record_key record, std::vector<std::byte> value) {
   auto active = co_await driver->begin_transaction();
   co_await active.put(std::move(family), std::move(record), std::move(value));
   co_await active.commit();
}

} // namespace

namespace db_revision_tests {

struct account_by_id;
struct account_by_name;
struct usage_by_id;
struct usage_by_state;
struct usage_bytes;

struct account : forge::db::object::object<account, 1, 1> {
   std::string name;

   bool operator==(const account&) const = default;
};

BOOST_DESCRIBE_STRUCT(account, (forge::db::object::object<account, 1, 1>), (name))

using account_object = forge::db::object::object_index<
    account, forge::db::object::indexed_by<
                 forge::db::object::primary_unique<account_by_id>,
                 forge::db::object::ordered_unique<account_by_name, forge::db::object::member<&account::name>>>>;

struct usage : forge::db::object::object<usage, 1, 2> {
   std::uint32_t state = 0;
   std::uint64_t bytes = 0;

   bool operator==(const usage&) const = default;
};

BOOST_DESCRIBE_STRUCT(usage, (forge::db::object::object<usage, 1, 2>), (state, bytes))

using usage_sum = forge::db::object::sum<usage_bytes, forge::db::object::member<&usage::bytes>>;

using usage_object = forge::db::object::object_index<
    usage, forge::db::object::indexed_by<
               forge::db::object::ranked_primary_unique<usage_by_id, forge::db::object::ranked_schema<1>, usage_sum>,
               forge::db::object::ranked_non_unique<usage_by_state, forge::db::object::member<&usage::state>,
                                                    forge::db::object::ranked_schema<1>, usage_sum>>>;

class counting_observer final : public forge::db::object::observer {
 public:
   boost::asio::awaitable<void> after_commit(const forge::db::object::change_set& changes) override {
      ++calls;
      mutation_count += changes.mutations.size();
      co_return;
   }

   std::size_t calls = 0;
   std::size_t mutation_count = 0;
};

} // namespace db_revision_tests

FORGE_DB_OBJECT(db_revision_tests::account_object)
FORGE_DB_OBJECT(db_revision_tests::usage_object)

static_assert(std::same_as<forge::db::object::index_for_id_t<forge::db::revision::state::id_t>,
                           forge::db::revision::state_index>);
static_assert(std::same_as<forge::db::object::index_for_id_t<forge::db::revision::entry::id_t>,
                           forge::db::revision::entry_index>);
static_assert(std::same_as<forge::db::object::index_for_id_t<forge::db::revision::delta::id_t>,
                           forge::db::revision::delta_index>);

BOOST_AUTO_TEST_SUITE(db_revision_test_suite)

BOOST_AUTO_TEST_CASE(db_revision_commits_before_images_and_reverts_only_current_head) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      const auto records = forge::db::core::family{"records"};

      auto first = co_await env.revisions.begin_transaction();
      BOOST_CHECK_EQUAL(first.id(), 1U);
      co_await first.db_transaction().put(records, key("item"), bytes("v1"));
      co_await first.commit();

      auto second = co_await env.revisions.begin_transaction();
      BOOST_CHECK_EQUAL(second.id(), 2U);
      co_await second.db_transaction().put(records, key("item"), bytes("v2"));
      co_await second.commit();

      auto current = co_await env.objects.get(forge::db::revision::state_id);
      BOOST_REQUIRE(current.head.has_value());
      BOOST_CHECK_EQUAL(*current.head, 2U);
      BOOST_CHECK_EQUAL(current.next_revision, 3U);

      auto stale = co_await env.driver->begin_transaction();
      BOOST_CHECK_THROW(co_await env.revisions.revert(stale, 1U), forge::db::revision::exceptions::stale_head);
      co_await stale.rollback();

      auto revert = co_await env.driver->begin_transaction();
      co_await env.revisions.revert(revert, 2U);
      co_await revert.commit();

      const auto restored = co_await read_record(env.driver, records, key("item"));
      BOOST_REQUIRE(restored.has_value());
      BOOST_CHECK_EQUAL(text(*restored), "v1");
      current = co_await env.objects.get(forge::db::revision::state_id);
      BOOST_REQUIRE(current.head.has_value());
      BOOST_CHECK_EQUAL(*current.head, 1U);
      BOOST_CHECK_EQUAL(current.next_revision, 3U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_rollback_reuses_candidate_and_commits_noop_revision) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();

      auto discarded = co_await env.revisions.begin_transaction();
      BOOST_CHECK_EQUAL(discarded.id(), 1U);
      co_await discarded.rollback();

      auto committed = co_await env.revisions.begin_transaction();
      BOOST_CHECK_EQUAL(committed.id(), 1U);
      co_await committed.commit();

      const auto metadata = co_await env.objects.get(forge::db::revision::entry::id_t{1U});
      BOOST_CHECK_EQUAL(metadata.delta_count, 0U);
      BOOST_CHECK_EQUAL(metadata.first_delta, 0U);
      const auto current = co_await env.objects.get(forge::db::revision::state_id);
      BOOST_REQUIRE(current.head.has_value());
      BOOST_CHECK_EQUAL(*current.head, 1U);
      BOOST_CHECK_EQUAL(current.next_revision, 2U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_serializes_concurrent_candidates_and_rechecks_locked_state) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto state = std::make_shared<memory_state>();
      auto env = co_await open_environment(state);
      const auto records = forge::db::core::family{"records"};

      auto first = co_await env.revisions.begin_transaction();
      BOOST_CHECK_EQUAL(first.id(), 1U);
      co_await first.db_transaction().put(records, key("first"), bytes("committed"));

      auto second_done = std::atomic_bool{false};
      auto second_id = forge::db::revision::revision_id_t{0};
      auto second_error = std::exception_ptr{};
      const auto executor = co_await boost::asio::this_coro::executor;
      boost::asio::co_spawn(
          executor,
          [&]() -> boost::asio::awaitable<void> {
             try {
                auto second = co_await env.revisions.begin_transaction();
                second_id = second.id();
                co_await second.rollback();
             } catch (...) {
                second_error = std::current_exception();
             }
             second_done.store(true, std::memory_order_release);
             co_return;
          },
          boost::asio::detached);

      auto wait = boost::asio::steady_timer{executor};
      for (auto attempt = 0; attempt < 50; ++attempt) {
         wait.expires_after(std::chrono::milliseconds{1});
         co_await wait.async_wait(boost::asio::use_awaitable);
      }
      BOOST_CHECK(!second_done.load(std::memory_order_acquire));
      {
         const auto lock = std::scoped_lock{state->mutex};
         BOOST_CHECK(state->lock_waiters.empty());
      }

      co_await first.commit();

      for (auto attempt = 0; attempt < 200 && !second_done.load(std::memory_order_acquire); ++attempt) {
         wait.expires_after(std::chrono::milliseconds{1});
         co_await wait.async_wait(boost::asio::use_awaitable);
      }
      BOOST_REQUIRE(second_done.load(std::memory_order_acquire));
      if (second_error) {
         std::rethrow_exception(second_error);
      }
      BOOST_CHECK_EQUAL(second_id, 2U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_join_reserves_object_writer_lane_before_locking_state) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      auto first = co_await env.driver->begin_transaction();
      const auto revision = co_await env.revisions.join(first);
      BOOST_CHECK_EQUAL(revision.id(), 1U);
      BOOST_CHECK_THROW(co_await env.revisions.join(first), forge::db::revision::exceptions::unsupported_operation);

      auto second = co_await env.driver->begin_transaction();
      auto second_objects = std::optional<forge::db::object::transaction>{};
      auto second_error = std::exception_ptr{};
      auto second_joined = std::atomic_bool{false};
      const auto executor = co_await boost::asio::this_coro::executor;
      boost::asio::co_spawn(
          executor,
          [&]() -> boost::asio::awaitable<void> {
             try {
                second_objects.emplace(co_await env.objects.join(second));
             } catch (...) {
                second_error = std::current_exception();
             }
             second_joined.store(true, std::memory_order_release);
             co_return;
          },
          boost::asio::detached);

      auto timer = boost::asio::steady_timer{executor};
      for (auto attempt = 0; attempt < 50 && !second_joined.load(std::memory_order_acquire); ++attempt) {
         timer.expires_after(std::chrono::milliseconds{1});
         co_await timer.async_wait(boost::asio::use_awaitable);
      }
      const auto joined_before_revision_closed = second_joined.load(std::memory_order_acquire);

      co_await first.rollback();
      for (auto attempt = 0; attempt < 500 && !second_joined.load(std::memory_order_acquire); ++attempt) {
         timer.expires_after(std::chrono::milliseconds{1});
         co_await timer.async_wait(boost::asio::use_awaitable);
      }
      BOOST_REQUIRE(second_joined.load(std::memory_order_acquire));
      if (second_error) {
         std::rethrow_exception(second_error);
      }
      BOOST_REQUIRE(second_objects.has_value());
      co_await second.rollback();

      BOOST_CHECK(!joined_before_revision_closed);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_core_join_reuses_existing_object_writer_lane) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      env.objects.register_object<db_revision_tests::account_object>();
      auto active = co_await env.driver->begin_transaction();
      auto objects = co_await env.objects.join(active);
      const auto revision = co_await env.revisions.join(active);

      BOOST_CHECK_EQUAL(revision.id(), 1U);
      const auto created = co_await objects.create<db_revision_tests::account>(
          [](db_revision_tests::account& value) { value.name = "joined-first"; });
      co_await active.commit();

      const auto stored = co_await env.objects.get(created.id);
      BOOST_CHECK_EQUAL(stored.name, "joined-first");
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_core_join_does_not_reuse_non_object_family_owner) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      auto blobs = forge::db::blob::store{env.driver, forge::db::blob::store::config{
                                                          .data_family = env.objects.family(),
                                                          .refs_family = forge::db::core::family{"blob.refs"},
                                                      }};

      auto active = co_await env.driver->begin_transaction();
      auto blob_tx = blobs.join(active);
      static_cast<void>(blob_tx);
      BOOST_CHECK_THROW(co_await env.revisions.join(active), forge::db::core::exceptions::participant_conflict);
      co_await active.rollback();
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_savepoint_discard_and_release_preserve_correct_before_image) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      const auto records = forge::db::core::family{"records"};
      co_await seed_record(env.driver, records, key("item"), bytes("original"));

      auto first = co_await env.revisions.begin_transaction();
      co_await first.db_transaction().put(records, key("item"), bytes("kept"));
      const auto discarded = co_await first.db_transaction().create_savepoint();
      co_await first.db_transaction().put(records, key("item"), bytes("discarded"));
      co_await first.db_transaction().put(records, key("temporary"), bytes("discarded"));
      co_await first.db_transaction().rollback_to_savepoint(discarded);
      co_await first.commit();

      auto read = co_await read_record(env.driver, records, key("item"));
      BOOST_REQUIRE(read.has_value());
      BOOST_CHECK_EQUAL(text(*read), "kept");
      BOOST_CHECK(!(co_await read_record(env.driver, records, key("temporary"))).has_value());

      auto revert_first = co_await env.driver->begin_transaction();
      co_await env.revisions.revert(revert_first, 1U);
      co_await revert_first.commit();

      auto second = co_await env.revisions.begin_transaction();
      const auto released = co_await second.db_transaction().create_savepoint();
      co_await second.db_transaction().put(records, key("item"), bytes("released"));
      co_await second.db_transaction().release_savepoint(released);
      co_await second.commit();

      auto revert_second = co_await env.driver->begin_transaction();
      co_await env.revisions.revert(revert_second, 2U);
      co_await revert_second.commit();
      read = co_await read_record(env.driver, records, key("item"));
      BOOST_REQUIRE(read.has_value());
      BOOST_CHECK_EQUAL(text(*read), "original");
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_revert_rejects_corrupt_journal_before_application_mutation) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      const auto records = forge::db::core::family{"records"};
      auto revision = co_await env.revisions.begin_transaction();
      co_await revision.db_transaction().put(records, key("item"), bytes("committed"));
      co_await revision.commit();

      auto corrupt = co_await env.driver->begin_transaction();
      co_await corrupt.put(
          env.objects.family(),
          forge::db::object::system::access::record_key(forge::db::revision::entry::id_t{1U}.as_object_id()),
          bytes("not-a-valid-entry"));
      co_await corrupt.commit();

      auto revert = co_await env.driver->begin_transaction();
      BOOST_CHECK_THROW(co_await env.revisions.revert(revert, 1U), forge::db::revision::exceptions::corrupt_state);
      co_await revert.rollback();

      const auto still_committed = co_await read_record(env.driver, records, key("item"));
      BOOST_REQUIRE(still_committed.has_value());
      BOOST_CHECK_EQUAL(text(*still_committed), "committed");
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_prune_is_bounded_and_removes_only_complete_revisions) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      const auto records = forge::db::core::family{"records"};
      for (auto id = 1U; id <= 3U; ++id) {
         auto revision = co_await env.revisions.begin_transaction();
         co_await revision.db_transaction().put(records, key("item-" + std::to_string(id)), bytes("value"));
         co_await revision.commit();
      }

      auto first_batch = co_await env.driver->begin_transaction();
      const auto first =
          co_await env.revisions.prune_through(first_batch, 2U, {.max_revisions = 1U, .max_deltas = 10U});
      BOOST_CHECK_EQUAL(first.revisions_pruned, 1U);
      BOOST_CHECK_EQUAL(first.deltas_pruned, 1U);
      BOOST_CHECK(!first.complete);
      co_await first_batch.commit();

      BOOST_CHECK(!(co_await env.objects.find(forge::db::revision::entry::id_t{1U})).has_value());
      BOOST_CHECK((co_await env.objects.find(forge::db::revision::entry::id_t{2U})).has_value());

      auto second_batch = co_await env.driver->begin_transaction();
      const auto second =
          co_await env.revisions.prune_through(second_batch, 2U, {.max_revisions = 1U, .max_deltas = 10U});
      BOOST_CHECK_EQUAL(second.revisions_pruned, 1U);
      BOOST_CHECK(second.complete);
      co_await second_batch.commit();

      const auto current = co_await env.objects.get(forge::db::revision::state_id);
      BOOST_CHECK_EQUAL(current.prune_baseline, 2U);
      BOOST_CHECK_EQUAL(current.oldest_retained, 3U);
      BOOST_REQUIRE(current.head.has_value());
      BOOST_CHECK_EQUAL(*current.head, 3U);
      BOOST_CHECK((co_await env.objects.find(forge::db::revision::entry::id_t{3U})).has_value());
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_prune_skips_committed_ids_removed_by_revert) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();

      auto first = co_await env.revisions.begin_transaction();
      co_await first.commit();
      auto reverted = co_await env.revisions.begin_transaction();
      co_await reverted.commit();

      auto revert = co_await env.driver->begin_transaction();
      co_await env.revisions.revert(revert, reverted.id());
      co_await revert.commit();

      auto third = co_await env.revisions.begin_transaction();
      BOOST_CHECK_EQUAL(third.id(), 3U);
      co_await third.commit();
      auto head = co_await env.revisions.begin_transaction();
      BOOST_CHECK_EQUAL(head.id(), 4U);
      co_await head.commit();

      auto first_batch = co_await env.driver->begin_transaction();
      const auto first_result =
          co_await env.revisions.prune_through(first_batch, third.id(), {.max_revisions = 1U, .max_deltas = 1U});
      BOOST_CHECK_EQUAL(first_result.revisions_pruned, 1U);
      BOOST_CHECK(!first_result.complete);
      co_await first_batch.commit();

      auto second_batch = co_await env.driver->begin_transaction();
      const auto second_result =
          co_await env.revisions.prune_through(second_batch, third.id(), {.max_revisions = 1U, .max_deltas = 1U});
      BOOST_CHECK_EQUAL(second_result.revisions_pruned, 1U);
      BOOST_CHECK(second_result.complete);
      co_await second_batch.commit();

      const auto current = co_await env.objects.get(forge::db::revision::state_id);
      BOOST_CHECK_EQUAL(current.prune_baseline, 3U);
      BOOST_CHECK_EQUAL(current.oldest_retained, 4U);
      BOOST_REQUIRE(current.head.has_value());
      BOOST_CHECK_EQUAL(*current.head, 4U);
      BOOST_CHECK(!(co_await env.objects.find(forge::db::revision::entry::id_t{2U})).has_value());
      BOOST_CHECK((co_await env.objects.find(forge::db::revision::entry::id_t{4U})).has_value());
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_cannot_revert_pruned_head_baseline) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();

      auto baseline = co_await env.revisions.begin_transaction();
      co_await baseline.commit();
      auto head = co_await env.revisions.begin_transaction();
      co_await head.commit();

      auto prune = co_await env.driver->begin_transaction();
      const auto pruned =
          co_await env.revisions.prune_through(prune, baseline.id(), {.max_revisions = 1U, .max_deltas = 1U});
      BOOST_CHECK(pruned.complete);
      co_await prune.commit();

      auto revert_head = co_await env.driver->begin_transaction();
      co_await env.revisions.revert(revert_head, head.id());
      co_await revert_head.commit();

      auto revert_baseline = co_await env.driver->begin_transaction();
      BOOST_CHECK_THROW(co_await env.revisions.revert(revert_baseline, baseline.id()),
                        forge::db::revision::exceptions::revision_pruned);
      co_await revert_baseline.rollback();
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_prune_rejects_too_small_delta_batch_without_mutation) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      const auto records = forge::db::core::family{"records"};
      auto first = co_await env.revisions.begin_transaction();
      co_await first.db_transaction().put(records, key("a"), bytes("one"));
      co_await first.db_transaction().put(records, key("b"), bytes("two"));
      co_await first.commit();
      auto second = co_await env.revisions.begin_transaction();
      co_await second.commit();

      auto prune = co_await env.driver->begin_transaction();
      BOOST_CHECK_THROW(co_await env.revisions.prune_through(prune, 1U, {.max_revisions = 1U, .max_deltas = 1U}),
                        forge::db::revision::exceptions::prune_limit_too_small);
      co_await prune.rollback();

      const auto current = co_await env.objects.get(forge::db::revision::state_id);
      BOOST_CHECK_EQUAL(current.prune_baseline, 0U);
      BOOST_CHECK((co_await env.objects.find(forge::db::revision::entry::id_t{1U})).has_value());
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_blob_retention_barrier_survives_collect_until_revert) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      auto blobs = forge::db::blob::store{env.driver};
      const auto owner = forge::db::blob::owner_ref{"document:1"};
      const auto payload = bytes("durable-payload");
      const auto value = co_await blobs.put(payload);
      co_await blobs.retain(value, owner);

      auto revision = co_await env.revisions.begin_transaction();
      auto blob_tx = blobs.join(revision.db_transaction());
      co_await blob_tx.release(value, owner);
      co_await revision.commit();
      BOOST_CHECK_EQUAL(co_await blobs.ref_count(value), 0U);

      const auto guarded = co_await blobs.collect_unreferenced({.limit = 10U});
      BOOST_CHECK_EQUAL(guarded.removed, 0U);
      BOOST_CHECK(co_await blobs.has(value));

      auto revert = co_await env.driver->begin_transaction();
      co_await env.revisions.revert(revert, 1U);
      co_await revert.commit();
      BOOST_CHECK_EQUAL(co_await blobs.ref_count(value), 1U);
      BOOST_CHECK(co_await blobs.get(value) == payload);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_coexists_with_independent_object_and_blob_participants) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      env.objects.register_object<db_revision_tests::account_object>();
      auto blobs = forge::db::blob::store{env.driver};

      auto active = co_await env.driver->begin_transaction();
      auto objects = co_await env.objects.join(active);
      auto revision = co_await env.revisions.join(objects);
      auto blob_tx = blobs.join(active);

      const auto created = co_await objects.create<db_revision_tests::account>(
          [](db_revision_tests::account& value) { value.name = "shared"; });
      const auto payload = co_await blob_tx.put(bytes("payload"));
      co_await active.commit();

      BOOST_CHECK_EQUAL(revision.id(), 1U);
      BOOST_CHECK_EQUAL((co_await env.objects.get(created.id)).name, "shared");
      BOOST_CHECK(co_await blobs.has(payload));
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_rejects_blob_payload_deletion_for_any_join_order) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      auto blobs = forge::db::blob::store{env.driver};
      const auto value = co_await blobs.put(bytes("protected"));

      auto active = co_await env.driver->begin_transaction();
      auto blob_tx = blobs.join(active);
      auto revision = co_await env.revisions.join(active);
      BOOST_CHECK_EQUAL(revision.id(), 1U);
      BOOST_CHECK_THROW(co_await blob_tx.erase(value), forge::db::core::exceptions::mutation_forbidden);
      co_await active.rollback();
      BOOST_CHECK(co_await blobs.has(value));
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_rejects_blob_collection_before_scan_or_erase) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      auto blobs = forge::db::blob::store{env.driver};
      const auto value = co_await blobs.put(bytes("retained"));
      co_await blobs.retain(value, forge::db::blob::owner_ref{"doc:retained"});

      auto active = co_await env.driver->begin_transaction();
      auto blob_tx = blobs.join(active);
      auto revision = co_await env.revisions.join(active);
      BOOST_CHECK_EQUAL(revision.id(), 1U);

      BOOST_CHECK_THROW(co_await blob_tx.collect_unreferenced({.limit = 0U}),
                        forge::db::core::exceptions::mutation_forbidden);
      BOOST_CHECK_THROW(co_await blob_tx.collect_unreferenced({.limit = 10U}),
                        forge::db::core::exceptions::mutation_forbidden);

      co_await active.rollback();
      BOOST_CHECK(co_await blobs.has(value));
      BOOST_CHECK_EQUAL(co_await blobs.ref_count(value), 1U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_reverts_generated_object_without_reusing_id_or_refiring_observer) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      env.objects.register_object<db_revision_tests::account_object>();
      auto observer = std::make_shared<db_revision_tests::counting_observer>();
      env.objects.add_observer(observer);

      auto active = co_await env.driver->begin_transaction();
      auto objects = co_await env.objects.join(active);
      auto revision = co_await env.revisions.join(objects);
      const auto created = co_await objects.create<db_revision_tests::account>(
          [](db_revision_tests::account& value) { value.name = "alice"; });
      BOOST_CHECK_EQUAL(created.id.instance, 0U);
      co_await active.commit();

      BOOST_CHECK_EQUAL(observer->calls, 1U);
      BOOST_CHECK_EQUAL(observer->mutation_count, 1U);
      BOOST_CHECK_EQUAL((co_await env.objects.get(created.id)).name, "alice");
      BOOST_REQUIRE(
          (co_await env.objects.index<db_revision_tests::account_object, db_revision_tests::account_by_name>().find(
               "alice"))
              .has_value());

      auto revert = co_await env.driver->begin_transaction();
      co_await env.revisions.revert(revert, revision.id());
      co_await revert.commit();

      BOOST_CHECK(!(co_await env.objects.find(created.id)).has_value());
      BOOST_CHECK(
          !(co_await env.objects.index<db_revision_tests::account_object, db_revision_tests::account_by_name>().find(
                "alice"))
               .has_value());
      BOOST_CHECK_EQUAL(observer->calls, 1U);

      const auto next = co_await env.objects.create<db_revision_tests::account>(
          [](db_revision_tests::account& value) { value.name = "bob"; });
      BOOST_CHECK_EQUAL(next.id.instance, 1U);
      BOOST_CHECK_EQUAL(observer->calls, 2U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_revert_restores_transactional_object_id_sequence) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto options = forge::db::object::store::options{
          .writes = forge::db::object::write_policy::single_writer,
          .id_allocation = forge::db::object::id_allocation_policy::transactional,
      };
      auto env = co_await open_environment(std::make_shared<memory_state>(), options);
      env.objects.register_object<db_revision_tests::account_object>();

      auto active = co_await env.driver->begin_transaction();
      auto objects = co_await env.objects.join(active);
      auto revision = co_await env.revisions.join(objects);
      const auto created = co_await objects.create<db_revision_tests::account>(
          [](db_revision_tests::account& value) { value.name = "alice"; });
      BOOST_CHECK_EQUAL(created.id.instance, 0U);
      co_await active.commit();

      auto revert = co_await env.driver->begin_transaction();
      co_await env.revisions.revert(revert, revision.id());
      co_await revert.commit();

      const auto replacement = co_await env.objects.create<db_revision_tests::account>(
          [](db_revision_tests::account& value) { value.name = "bob"; });
      BOOST_CHECK_EQUAL(replacement.id.instance, 0U);
      BOOST_CHECK_EQUAL((co_await env.objects.get(replacement.id)).name, "bob");
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_revert_restores_ranked_positions_and_aggregates) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto env = co_await open_environment();
      env.objects.register_object<db_revision_tests::usage_object>();

      auto baseline = db_revision_tests::usage{};
      baseline.id = db_revision_tests::usage::id_t{1U};
      baseline.state = 1U;
      baseline.bytes = 10U;
      co_await env.objects.insert(baseline);

      auto active = co_await env.driver->begin_transaction();
      auto objects = co_await env.objects.join(active);
      auto revision = co_await env.revisions.join(objects);

      auto moved = baseline;
      moved.state = 3U;
      moved.bytes = 15U;
      co_await objects.replace(moved);

      auto added = db_revision_tests::usage{};
      added.id = db_revision_tests::usage::id_t{2U};
      added.state = 2U;
      added.bytes = 20U;
      co_await objects.insert(added);
      co_await active.commit();

      auto current = env.objects.index<db_revision_tests::usage_object, db_revision_tests::usage_by_state>();
      BOOST_CHECK_EQUAL(co_await current.count(), 2U);
      BOOST_CHECK_EQUAL(co_await current.sum<db_revision_tests::usage_bytes>(), 35U);
      BOOST_CHECK_EQUAL((co_await current.nth(0))->id.instance, 2U);
      BOOST_CHECK_EQUAL((co_await current.nth(1))->id.instance, 1U);

      auto revert = co_await env.driver->begin_transaction();
      co_await env.revisions.revert(revert, revision.id());
      co_await revert.commit();

      BOOST_CHECK_EQUAL(co_await current.count(), 1U);
      BOOST_CHECK_EQUAL(co_await current.sum<db_revision_tests::usage_bytes>(), 10U);
      const auto restored = co_await current.nth(0);
      BOOST_REQUIRE(restored.has_value());
      BOOST_CHECK_EQUAL(restored->id.instance, 1U);
      BOOST_CHECK_EQUAL(restored->state, 1U);
      BOOST_CHECK_EQUAL(restored->bytes, 10U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_revision_requires_record_lock_capability) {
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto state = std::make_shared<memory_state>();
      state->record_locks = false;
      auto driver = std::make_shared<memory_driver>(state);
      auto objects = co_await forge::db::object::store::open(driver);
      BOOST_CHECK_THROW(co_await forge::db::revision::store::open(driver, objects),
                        forge::db::revision::exceptions::unsupported_operation);
      co_return;
   }());
}

BOOST_AUTO_TEST_SUITE_END()
