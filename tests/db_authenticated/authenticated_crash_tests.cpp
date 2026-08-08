#include <boost/asio/awaitable.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

import forge.asio.affine;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.authenticated.proof;
import forge.db.authenticated.store;
import forge.db.authenticated.types;
import forge.db.core.record;
import forge.db.mdbx.driver;
import forge.db.object.store;
import forge.db.revision.store;
import forge.db.revision.types;
import forge.exceptions;

#ifndef FORGE_DB_AUTHENTICATED_CRASH_HELPER
#define FORGE_DB_AUTHENTICATED_CRASH_HELPER ""
#endif

namespace {

using namespace std::chrono_literals;

forge::db::authenticated::bytes bytes(std::string value) {
   return {
       reinterpret_cast<const std::byte*>(value.data()),
       reinterpret_cast<const std::byte*>(value.data() + value.size()),
   };
}

std::string text(const forge::db::authenticated::bytes& value) {
   return {
       reinterpret_cast<const char*>(value.data()),
       reinterpret_cast<const char*>(value.data() + value.size()),
   };
}

std::filesystem::path make_crash_root(std::string name) {
   const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
   auto root = std::filesystem::temp_directory_path() / (std::move(name) + "_" + std::to_string(stamp));
   std::filesystem::remove_all(root);
   std::filesystem::create_directories(root);
   return root;
}

class child_process {
 public:
   explicit child_process(pid_t value) : process_{value} {}
   child_process(const child_process&) = delete;
   child_process& operator=(const child_process&) = delete;

   child_process(child_process&& other) noexcept
       : process_{std::exchange(other.process_, -1)}, reaped_{std::exchange(other.reaped_, true)},
         status_{other.status_} {}

   ~child_process() {
      if (!reaped_ && process_ > 0) {
         static_cast<void>(::kill(process_, SIGKILL));
         while (::waitpid(process_, &status_, 0) < 0 && errno == EINTR) {
         }
      }
   }

   [[nodiscard]] bool running() {
      if (reaped_) {
         return false;
      }
      const auto result = ::waitpid(process_, &status_, WNOHANG);
      if (result == 0) {
         return true;
      }
      if (result == process_) {
         reaped_ = true;
         return false;
      }
      if (result < 0 && errno == EINTR) {
         return running();
      }
      throw std::system_error{errno, std::generic_category(), "poll authenticated crash helper"};
   }

   void kill_and_reap() {
      BOOST_REQUIRE(!reaped_);
      BOOST_REQUIRE(::kill(process_, SIGKILL) == 0);
      while (::waitpid(process_, &status_, 0) < 0) {
         BOOST_REQUIRE(errno == EINTR);
      }
      reaped_ = true;
      BOOST_REQUIRE(WIFSIGNALED(status_));
      BOOST_TEST(WTERMSIG(status_) == SIGKILL);
   }

 private:
   pid_t process_ = -1;
   bool reaped_ = false;
   int status_ = 0;
};

child_process start_helper(const std::filesystem::path& root, std::string mode) {
   BOOST_REQUIRE(std::string_view{FORGE_DB_AUTHENTICATED_CRASH_HELPER}.size() > 0U);
   auto arguments = std::vector<std::string>{FORGE_DB_AUTHENTICATED_CRASH_HELPER, root.string(), std::move(mode)};
   auto native = std::vector<char*>{};
   for (auto& argument : arguments) {
      native.push_back(argument.data());
   }
   native.push_back(nullptr);

   const auto process = ::fork();
   BOOST_REQUIRE(process >= 0);
   if (process == 0) {
      ::execv(native.front(), native.data());
      _exit(127);
   }
   return child_process{process};
}

template <typename Predicate>
std::string wait_for_checkpoint(child_process& process, const std::filesystem::path& root, Predicate&& accept) {
   const auto deadline = std::chrono::steady_clock::now() + 15s;
   auto observed = std::string{};
   while (std::chrono::steady_clock::now() < deadline) {
      auto stream = std::ifstream{root / "checkpoint"};
      stream >> observed;
      if (accept(observed)) {
         return observed;
      }
      BOOST_REQUIRE_MESSAGE(process.running(), "authenticated crash helper exited before checkpoint");
      std::this_thread::sleep_for(5ms);
   }
   BOOST_FAIL("authenticated crash helper did not publish the expected checkpoint; observed " << observed);
   return {};
}

void stop_at_checkpoint(child_process& process, const std::filesystem::path& root, std::string_view expected) {
   static_cast<void>(wait_for_checkpoint(process, root, [&](const auto& value) { return value == expected; }));
   process.kill_and_reap();
}

struct recovery_state {
   std::uint64_t version = 0;
   std::string value;
   std::uint64_t revision = 0;
};

class post_crash_lock_recovery_policy {
 public:
   post_crash_lock_recovery_policy(std::chrono::steady_clock::time_point started,
                                   std::chrono::steady_clock::duration timeout)
       : deadline_{started + timeout} {}

   [[nodiscard]] bool should_retry(const forge::db::mdbx::exceptions::environment_busy& error,
                                   std::chrono::steady_clock::time_point now) const noexcept {
      if (now >= deadline_) {
         return false;
      }
      for (const auto& field : error.context()) {
         if (field.key == "native-code" && field.value == std::to_string(EAGAIN)) {
            return true;
         }
      }
      return false;
   }

 private:
   std::chrono::steady_clock::time_point deadline_;
};

boost::asio::awaitable<std::shared_ptr<forge::db::mdbx::driver>>
open_after_reaped_crash(const std::filesystem::path& root, forge::asio::affine::executor executor) {
   const auto recovery = post_crash_lock_recovery_policy{std::chrono::steady_clock::now(), 1s};
   for (;;) {
      try {
         co_return co_await forge::db::mdbx::driver::open(
             {
                 .path = (root / "store").string(),
                 .families = {"authenticated", "objectdb"},
                 .durability_mode = forge::db::mdbx::durability::durable_sync,
                 .create_if_missing = false,
                 .create_missing_families = false,
             },
             executor);
      } catch (const forge::db::mdbx::exceptions::environment_busy& error) {
         // MDBX documents that POSIX DXB lock acquisition can briefly collide
         // with file-lock recovery after a process dies.
         if (!recovery.should_retry(error, std::chrono::steady_clock::now())) {
            throw;
         }
         std::this_thread::sleep_for(5ms);
      }
   }
}

void verify_reopen(const std::filesystem::path& root, std::initializer_list<recovery_state> allowed) {
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};
   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await open_after_reaped_crash(root, lane.get_executor());
      auto objects = co_await forge::db::object::store::open(driver);
      static_cast<void>(co_await forge::db::revision::store::open(driver, objects));
      auto authenticated = forge::db::authenticated::store{
          driver,
          {
              .family = forge::db::core::family{"authenticated"},
              .domain = "forge.test.authenticated.crash.v1",
          },
      };

      const auto root_record = co_await authenticated.latest();
      BOOST_REQUIRE(root_record.has_value());
      const auto expected = std::ranges::find(allowed, root_record->version, &recovery_state::version);
      BOOST_REQUIRE_MESSAGE(expected != allowed.end(), "unexpected recovered authenticated version");
      const auto version_root = co_await authenticated.find_root(expected->version);
      BOOST_REQUIRE(version_root.has_value());
      BOOST_CHECK(*root_record == *version_root);
      BOOST_TEST(!(co_await authenticated.find_root(expected->version + 1U)).has_value());
      const auto revision_state = co_await objects.get(forge::db::revision::state_id);
      BOOST_REQUIRE(revision_state.head.has_value());
      BOOST_TEST(*revision_state.head == expected->revision);
      BOOST_TEST(text(*(co_await authenticated.get(expected->version, bytes("value")))) == expected->value);
      const auto proof = co_await authenticated.prove(expected->version, bytes("value"));
      BOOST_CHECK(proof.anchor == *version_root);
      const auto verified =
          forge::db::authenticated::verify_point("forge.test.authenticated.crash.v1", *version_root, proof.key, proof);
      BOOST_TEST(verified.exists);
      BOOST_REQUIRE(verified.value.has_value());
      BOOST_TEST(text(*verified.value) == expected->value);

      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());
}

void run_case(std::string name, std::string mode, std::uint64_t expected_version, std::string expected_value,
              std::uint64_t expected_revision) {
   const auto root = make_crash_root(std::move(name));
   auto process = start_helper(root, mode);
   stop_at_checkpoint(process, root, mode);
   verify_reopen(root, {{expected_version, std::move(expected_value), expected_revision}});
   std::filesystem::remove_all(root);
}

void run_commit_race_case() {
   const auto root = make_crash_root("forge_db_authenticated_commit_race");
   auto process = start_helper(root, "racing");
   const auto checkpoint =
       wait_for_checkpoint(process, root, [](const auto& value) { return value.starts_with("commit-started:"); });
   const auto candidate = std::stoull(checkpoint.substr(std::string_view{"commit-started:"}.size()));
   BOOST_REQUIRE(candidate > 0U);
   process.kill_and_reap();

   const auto previous = candidate - 1U;
   verify_reopen(root,
                 {
                     {previous, previous == 0U ? "initial" : "candidate-" + std::to_string(previous), previous + 1U},
                     {candidate, "candidate-" + std::to_string(candidate), candidate + 1U},
                 });
   std::filesystem::remove_all(root);
}

} // namespace

BOOST_AUTO_TEST_SUITE(db_authenticated_crash_test_suite)

BOOST_AUTO_TEST_CASE(post_crash_reopen_retries_only_native_eagain_before_deadline) {
   const auto started = std::chrono::steady_clock::time_point{};
   const auto policy = post_crash_lock_recovery_policy{started, 25ms};
   const auto transient = forge::db::mdbx::exceptions::environment_busy{
       "transient test lock", {forge::exceptions::ctx("native-code", EAGAIN)}};
   const auto other_busy = forge::db::mdbx::exceptions::environment_busy{
       "non-transient test lock", {forge::exceptions::ctx("native-code", EBUSY)}};

   BOOST_TEST(policy.should_retry(transient, started));
   BOOST_TEST(!policy.should_retry(other_busy, started));
   BOOST_TEST(!policy.should_retry(transient, started + 25ms));
}

BOOST_AUTO_TEST_CASE(staged_authenticated_state_and_revision_are_absent_after_process_crash) {
   run_case("forge_db_authenticated_crash_staged", "staged", 0, "initial", 1);
}

BOOST_AUTO_TEST_CASE(committed_authenticated_state_and_revision_survive_process_crash) {
   run_case("forge_db_authenticated_crash_committed", "committed", 1, "candidate", 2);
}

BOOST_AUTO_TEST_CASE(shared_authenticated_and_revision_commit_is_atomic_when_process_is_killed) {
   run_commit_race_case();
}

BOOST_AUTO_TEST_SUITE_END()
