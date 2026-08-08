#include <boost/asio/awaitable.hpp>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

import forge.asio.affine;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.authenticated.store;
import forge.db.authenticated.transaction;
import forge.db.authenticated.types;
import forge.db.core.record;
import forge.db.mdbx.driver;
import forge.db.object.store;
import forge.db.revision.store;

namespace {

using namespace std::chrono_literals;

forge::db::authenticated::bytes bytes(std::string value) {
   return {
       reinterpret_cast<const std::byte*>(value.data()),
       reinterpret_cast<const std::byte*>(value.data() + value.size()),
   };
}

forge::db::authenticated::mutation put(std::string key, std::string value) {
   return {
       .key = bytes(std::move(key)),
       .value = bytes(std::move(value)),
   };
}

void require_system_call(int result, std::string_view operation) {
   if (result < 0) {
      throw std::system_error{errno, std::generic_category(), std::string{operation}};
   }
}

void write_all(int descriptor, std::string_view value) {
   auto written = std::size_t{};
   while (written < value.size()) {
      const auto result = ::write(descriptor, value.data() + written, value.size() - written);
      if (result < 0 && errno == EINTR) {
         continue;
      }
      require_system_call(static_cast<int>(result), "write crash checkpoint");
      written += static_cast<std::size_t>(result);
   }
}

void publish_checkpoint(const std::filesystem::path& root, std::string_view value) {
   const auto target = root / "checkpoint";
   const auto temporary = root / "checkpoint.tmp";
   const auto descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
   require_system_call(descriptor, "open crash checkpoint");
   try {
      write_all(descriptor, value);
      require_system_call(::fsync(descriptor), "sync crash checkpoint");
   } catch (...) {
      static_cast<void>(::close(descriptor));
      throw;
   }
   require_system_call(::close(descriptor), "close crash checkpoint");
   require_system_call(::rename(temporary.c_str(), target.c_str()), "publish crash checkpoint");
}

} // namespace

int main(int argc, char** argv) try {
   if (argc != 3) {
      return 2;
   }

   const auto root = std::filesystem::path{argv[1]};
   const auto mode = std::string_view{argv[2]};
   if (mode != "staged" && mode != "committed" && mode != "racing") {
      return 2;
   }

   std::filesystem::create_directories(root);
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{{.thread_name = "authenticated-crash"}};
   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await forge::db::mdbx::driver::open(
          {
              .path = (root / "store").string(),
              .families = {"authenticated", "objectdb"},
              .durability_mode = forge::db::mdbx::durability::durable_sync,
          },
          lane.get_executor());
      auto objects = co_await forge::db::object::store::open(driver);
      auto revisions = co_await forge::db::revision::store::open(driver, objects);
      auto authenticated = forge::db::authenticated::store{
          driver,
          {
              .family = forge::db::core::family{"authenticated"},
              .domain = "forge.test.authenticated.crash.v1",
          },
      };

      auto initial = co_await driver->begin_transaction();
      static_cast<void>(co_await revisions.join(initial));
      auto initial_authenticated = co_await authenticated.join(initial, 0);
      static_cast<void>(co_await initial_authenticated.stage(
          std::vector<forge::db::authenticated::mutation>{put("value", "initial")}));
      co_await initial.commit();

      if (mode == "racing") {
         for (auto version = std::uint64_t{1};; ++version) {
            auto candidate = co_await driver->begin_transaction();
            static_cast<void>(co_await revisions.join(candidate));
            auto candidate_authenticated = co_await authenticated.join(candidate, version);
            auto mutations = std::vector<forge::db::authenticated::mutation>{};
            mutations.reserve(1'025U);
            mutations.push_back(put("value", "candidate-" + std::to_string(version)));
            for (auto index = std::uint32_t{}; index < 1'024U; ++index) {
               mutations.push_back(
                   put("padding-" + std::to_string(index), std::string(128U, static_cast<char>('a' + version % 26U))));
            }
            static_cast<void>(co_await candidate_authenticated.stage(std::move(mutations)));
            publish_checkpoint(root, "commit-started:" + std::to_string(version));
            co_await candidate.commit();
            publish_checkpoint(root, "commit-finished:" + std::to_string(version));
         }
      }

      auto candidate = co_await driver->begin_transaction();
      static_cast<void>(co_await revisions.join(candidate));
      auto candidate_authenticated = co_await authenticated.join(candidate, 1);
      static_cast<void>(co_await candidate_authenticated.stage(
          std::vector<forge::db::authenticated::mutation>{put("value", "candidate")}));
      if (mode == "committed") {
         co_await candidate.commit();
      }
      publish_checkpoint(root, mode);
      for (;;) {
         std::this_thread::sleep_for(1s);
      }
   }());
   return 0;
} catch (...) {
   return 1;
}
