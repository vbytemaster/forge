#include <boost/test/unit_test.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.http.route_context;
import fcl.http.server;
import fcl.http.types;
import fcl.log.log_message;
import fcl.log.record;
import fcl.otlp;

#ifndef FCL_OTLP_CRASH_HELPER
#define FCL_OTLP_CRASH_HELPER ""
#endif

namespace {

using namespace std::chrono_literals;

struct collector_response {
   fcl::http::status status = fcl::http::status::ok;
   std::string body = "{}";
   std::optional<std::string> retry_after;
};

struct collected_request {
   std::string target;
   std::string content_type;
   std::string body;
};

class fake_collector {
 public:
   fake_collector(fcl::asio::runtime& runtime, std::vector<collector_response> responses, bool block_responses = false)
       : responses_(std::move(responses)),
         block_responses_(block_responses),
         server_(runtime, {.bind_address = "127.0.0.1", .port = 0},
                 [this](fcl::http::route_context& context) { return handle(context); }) {
      if (responses_.empty()) {
         responses_.push_back({});
      }
      server_.start();
      for (auto attempt = 0; attempt != 100; ++attempt) {
         if (server_.port() != 0) {
            return;
         }
         std::this_thread::sleep_for(10ms);
      }
      BOOST_FAIL("fake OTLP collector did not bind a port");
   }

   ~fake_collector() {
      release_responses();
      server_.stop();
      // fcl::http::server::stop() closes accept, while active sessions finish on runtime workers.
      std::this_thread::sleep_for(20ms);
   }

   [[nodiscard]] std::string endpoint() const {
      return "http://127.0.0.1:" + std::to_string(server_.port());
   }

   [[nodiscard]] std::vector<collected_request> requests() const {
      const auto lock = std::scoped_lock{mutex_};
      return requests_;
   }

   [[nodiscard]] bool wait_for_requests(std::size_t count, std::chrono::milliseconds timeout = 2s) const {
      auto lock = std::unique_lock{mutex_};
      return ready_.wait_for(lock, timeout, [&] { return requests_.size() >= count; });
   }

   void release_responses() {
      {
         const auto lock = std::scoped_lock{mutex_};
         release_responses_ = true;
      }
      response_ready_.notify_all();
   }

 private:
   fcl::http::response handle(fcl::http::route_context& context) {
      auto request = collected_request{
          .target = std::string{context.request.target()},
          .body = context.request.body(),
      };
      if (const auto header = context.request.find(fcl::http::field::content_type);
          header != context.request.end()) {
         request.content_type = std::string{header->value()};
      }

      auto response_value = collector_response{};
      {
         const auto lock = std::scoped_lock{mutex_};
         requests_.push_back(std::move(request));
         const auto index = requests_.size() - 1;
         response_value = responses_[std::min(index, responses_.size() - 1)];
      }
      ready_.notify_all();

      if (block_responses_) {
         auto lock = std::unique_lock{mutex_};
         response_ready_.wait(lock, [&] { return release_responses_; });
      }

      auto response = fcl::http::make_text_response(context.request, response_value.status, response_value.body,
                                                    "application/json");
      if (response_value.retry_after.has_value()) {
         response.set(fcl::http::field::retry_after, *response_value.retry_after);
      }
      return response;
   }

   std::vector<collector_response> responses_;
   bool block_responses_ = false;
   bool release_responses_ = false;
   fcl::http::server server_;
   mutable std::mutex mutex_;
   mutable std::condition_variable ready_;
   mutable std::condition_variable response_ready_;
   std::vector<collected_request> requests_;
};

fcl::log_record make_record(std::string message = "cache miss") {
   return fcl::log_record{
       .level = fcl::log_level::warn,
       .logger = "test.otlp",
       .component = "cache",
       .message = std::move(message),
       .fields = {fcl::log_ctx("peer", "server-a"), fcl::log_secret("token", "secret-value")},
       .timestamp = std::chrono::sys_time<std::chrono::microseconds>{std::chrono::seconds{1}},
       .thread_id = "thread-7",
       .thread_name = "worker",
       .exception_chain = "typed failure",
   };
}

fcl::otlp::log_exporter_options make_options(const fake_collector& collector) {
   return fcl::otlp::log_exporter_options{
       .endpoint = collector.endpoint(),
       .resource = {.attributes = {{"service.name", "fcl-test"}, {"service.version", "1.0.0"}}},
       .batch = {.max_records = 10, .max_bytes = 64 * 1024, .flush_interval = 1h},
       .queue = {.max_records = 100, .max_bytes = 1024 * 1024},
       .retry = {.max_attempts = 0, .base_delay = 1ms, .max_delay = 10ms},
       .request_timeout = 500ms,
       .shutdown_timeout = 500ms,
   };
}

void expect_contains(std::string_view haystack, std::string_view needle) {
   BOOST_TEST_CONTEXT("needle: " << needle) {
      BOOST_TEST(haystack.find(needle) != std::string_view::npos);
   }
}

void expect_not_contains(std::string_view haystack, std::string_view needle) {
   BOOST_TEST_CONTEXT("needle: " << needle) {
      BOOST_TEST(haystack.find(needle) == std::string_view::npos);
   }
}

class temp_directory {
 public:
   explicit temp_directory(std::string_view name) {
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path() /
              (std::string{name} + "-" + std::to_string(static_cast<long long>(stamp)));
      std::filesystem::remove_all(path_);
      std::filesystem::create_directories(path_);
   }

   ~temp_directory() {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
   }

   [[nodiscard]] const std::filesystem::path& path() const noexcept {
      return path_;
   }

 private:
   std::filesystem::path path_;
};

std::string shell_quote(std::string_view value) {
   auto quoted = std::string{"'"};
   for (const auto ch : value) {
      if (ch == '\'') {
         quoted += "'\\''";
      } else {
         quoted.push_back(ch);
      }
   }
   quoted.push_back('\'');
   return quoted;
}

int run_crash_helper(std::string_view mode, const std::filesystem::path& directory) {
   BOOST_REQUIRE(std::string_view{FCL_OTLP_CRASH_HELPER}.size() > 0);
   const auto command =
       shell_quote(FCL_OTLP_CRASH_HELPER) + " " + shell_quote(mode) + " " + shell_quote(directory.string());
   return std::system(command.c_str());
}

std::size_t count_spool_files(const std::filesystem::path& directory) {
   auto count = std::size_t{0};
   for (const auto& entry : std::filesystem::directory_iterator{directory}) {
      if (entry.path().extension() == ".spool") {
         ++count;
      }
   }
   return count;
}

std::filesystem::path first_spool_file(const std::filesystem::path& directory) {
   for (const auto& entry : std::filesystem::directory_iterator{directory}) {
      if (entry.path().extension() == ".spool") {
         return entry.path();
      }
   }
   BOOST_FAIL("expected at least one crash spool file");
   return {};
}

void append_file_bytes(const std::filesystem::path& source, const std::filesystem::path& target) {
   auto input = std::ifstream{source, std::ios::binary};
   BOOST_REQUIRE(input.good());
   auto output = std::ofstream{target, std::ios::binary | std::ios::app};
   BOOST_REQUIRE(output.good());
   output << input.rdbuf();
}

fcl::otlp::crash_spool_options make_spool_options(const std::filesystem::path& directory) {
   return fcl::otlp::crash_spool_options{
       .directory = directory,
       .max_record_bytes = 4096,
       .max_records_per_process = 8,
       .max_records_per_resend = 1024,
       .max_file_bytes = 256 * 1024,
       .capture_signals = true,
       .capture_terminate = true,
   };
}

} // namespace

BOOST_AUTO_TEST_SUITE(otlp_test_suite)

BOOST_AUTO_TEST_CASE(log_sink_exports_otlp_json_to_logs_endpoint) {
   auto runtime = fcl::asio::runtime{};
   auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}};
   auto exporter = std::make_shared<fcl::otlp::log_exporter>(runtime, make_options(collector));
   auto sink = fcl::otlp::log_sink{exporter};

   sink.log(make_record());
   fcl::asio::blocking::run(runtime, exporter->async_flush());

   BOOST_REQUIRE(collector.wait_for_requests(1));
   const auto requests = collector.requests();
   BOOST_REQUIRE_EQUAL(requests.size(), 1U);
   BOOST_TEST(requests.front().target == "/v1/logs");
   BOOST_TEST(requests.front().content_type == "application/json");

   const auto& body = requests.front().body;
   expect_contains(body, "\"resourceLogs\"");
   expect_contains(body, "\"scopeLogs\"");
   expect_contains(body, "\"logRecords\"");
   expect_contains(body, "\"service.name\"");
   expect_contains(body, "\"fcl-test\"");
   expect_contains(body, "\"severityText\":\"WARN\"");
   expect_contains(body, "\"severityNumber\":13");
   expect_contains(body, "\"timeUnixNano\":\"1000000000\"");
   expect_contains(body, "\"body\":{\"stringValue\":\"cache miss\"}");
   expect_contains(body, "\"peer\"");
   expect_contains(body, "\"server-a\"");
   expect_contains(body, "\"token\"");
   expect_contains(body, "\"<redacted>\"");
   expect_contains(body, "\"exception.chain\"");
   expect_not_contains(body, "secret-value");

   const auto metrics = exporter->metrics();
   BOOST_TEST(metrics.enqueued_records == 1U);
   BOOST_TEST(metrics.exported_records == 1U);
   BOOST_TEST(metrics.dropped_records == 0U);

   fcl::asio::blocking::run(runtime, exporter->async_shutdown());
}

BOOST_AUTO_TEST_CASE(exporter_batches_by_count_and_explicit_flush) {
   auto runtime = fcl::asio::runtime{};
   auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}, {.status = fcl::http::status::ok}}};
   auto options = make_options(collector);
   options.batch.max_records = 2;
   options.batch.flush_interval = 1h;

   auto exporter = std::make_shared<fcl::otlp::log_exporter>(runtime, options);
   auto sink = fcl::otlp::log_sink{exporter};

   sink.log(make_record("first"));
   std::this_thread::sleep_for(50ms);
   BOOST_TEST(collector.requests().empty());

   sink.log(make_record("second"));
   BOOST_REQUIRE(collector.wait_for_requests(1));
   expect_contains(collector.requests().front().body, "first");
   expect_contains(collector.requests().front().body, "second");

   sink.log(make_record("third"));
   fcl::asio::blocking::run(runtime, exporter->async_flush());
   BOOST_REQUIRE(collector.wait_for_requests(2));
   expect_contains(collector.requests().back().body, "third");

   const auto metrics = exporter->metrics();
   BOOST_TEST(metrics.exported_records == 3U);
   fcl::asio::blocking::run(runtime, exporter->async_shutdown());
}

BOOST_AUTO_TEST_CASE(bounded_queue_drops_newest_without_blocking_logger) {
   auto runtime = fcl::asio::runtime{};
   auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}};
   auto options = make_options(collector);
   options.queue.max_records = 1;
   options.batch.max_records = 10;
   options.batch.flush_interval = 1h;

   auto exporter = std::make_shared<fcl::otlp::log_exporter>(runtime, options);
   auto sink = fcl::otlp::log_sink{exporter};

   sink.log(make_record("kept"));
   sink.log(make_record("dropped"));
   fcl::asio::blocking::run(runtime, exporter->async_flush());

   BOOST_REQUIRE(collector.wait_for_requests(1));
   const auto body = collector.requests().front().body;
   expect_contains(body, "kept");
   expect_not_contains(body, "dropped");

   const auto metrics = exporter->metrics();
   BOOST_TEST(metrics.enqueued_records == 1U);
   BOOST_TEST(metrics.dropped_records == 1U);
   BOOST_TEST(metrics.exported_records == 1U);

   fcl::asio::blocking::run(runtime, exporter->async_shutdown());
}

BOOST_AUTO_TEST_CASE(exporter_retries_retryable_status_and_drops_permanent_failure) {
   {
      auto runtime = fcl::asio::runtime{};
      auto collector = fake_collector{runtime,
                                      {{.status = fcl::http::status::service_unavailable, .retry_after = "0"},
                                       {.status = fcl::http::status::ok}}};
      auto options = make_options(collector);
      options.retry.max_attempts = 2;

      auto exporter = std::make_shared<fcl::otlp::log_exporter>(runtime, options);
      auto sink = fcl::otlp::log_sink{exporter};
      sink.log(make_record("retryable"));
      fcl::asio::blocking::run(runtime, exporter->async_flush());

      BOOST_REQUIRE(collector.wait_for_requests(2));
      const auto metrics = exporter->metrics();
      BOOST_TEST(metrics.retry_attempts == 1U);
      BOOST_TEST(metrics.exported_records == 1U);
      BOOST_TEST(metrics.failed_records == 0U);
      fcl::asio::blocking::run(runtime, exporter->async_shutdown());
   }

   {
      auto runtime = fcl::asio::runtime{};
      auto collector =
          fake_collector{runtime, {{.status = fcl::http::status::bad_request}, {.status = fcl::http::status::ok}}};
      auto options = make_options(collector);
      options.retry.max_attempts = 2;

      auto exporter = std::make_shared<fcl::otlp::log_exporter>(runtime, options);
      auto sink = fcl::otlp::log_sink{exporter};
      sink.log(make_record("permanent"));
      fcl::asio::blocking::run(runtime, exporter->async_flush());

      std::this_thread::sleep_for(50ms);
      BOOST_REQUIRE_EQUAL(collector.requests().size(), 1U);
      const auto metrics = exporter->metrics();
      BOOST_TEST(metrics.exported_records == 0U);
      BOOST_TEST(metrics.failed_records == 1U);
      BOOST_TEST(metrics.dropped_records == 1U);
      fcl::asio::blocking::run(runtime, exporter->async_shutdown());
   }
}

BOOST_AUTO_TEST_CASE(shutdown_flushes_or_drops_within_deadline) {
   auto runtime = fcl::asio::runtime{};
   auto collector = fake_collector{runtime, {{.status = fcl::http::status::service_unavailable, .retry_after = "5"}}};
   auto options = make_options(collector);
   options.retry.max_attempts = 100;
   options.shutdown_timeout = 20ms;

   auto exporter = std::make_shared<fcl::otlp::log_exporter>(runtime, options);
   auto sink = fcl::otlp::log_sink{exporter};
   sink.log(make_record("shutdown"));

   const auto started = std::chrono::steady_clock::now();
   fcl::asio::blocking::run(runtime, exporter->async_shutdown());
   const auto elapsed = std::chrono::steady_clock::now() - started;

   BOOST_TEST(elapsed < 500ms);
   BOOST_REQUIRE(collector.wait_for_requests(1));
   const auto metrics = exporter->metrics();
   BOOST_TEST(metrics.exported_records == 0U);
   BOOST_TEST(metrics.dropped_records == 1U);
}

BOOST_AUTO_TEST_CASE(crash_spool_options_and_single_active_guard_are_typed) {
   auto invalid = fcl::otlp::crash_spool_options{};
   invalid.directory.clear();
   BOOST_CHECK_THROW((void)fcl::otlp::install_crash_capture(invalid), fcl::otlp::exceptions::invalid_options);

   auto directory = temp_directory{"fcl-otlp-crash-guard"};
   auto options = make_spool_options(directory.path());
   options.capture_terminate = false;
   auto guard = fcl::otlp::install_crash_capture(options);

   BOOST_CHECK_THROW((void)fcl::otlp::install_crash_capture(options), fcl::otlp::exceptions::capture_active);
}

#if defined(__unix__) || defined(__APPLE__)
BOOST_AUTO_TEST_CASE(crash_spool_rejects_symlink_and_writable_directory) {
   {
      auto directory = temp_directory{"fcl-otlp-crash-symlink"};
      const auto target = directory.path() / "target";
      {
         auto file = std::ofstream{target, std::ios::binary};
         file << "untouched";
      }
      std::filesystem::create_symlink(target, directory.path() / ("crash-" + std::to_string(::getpid()) + ".spool"));

      auto options = make_spool_options(directory.path());
      options.capture_signals = false;
      BOOST_CHECK_THROW((void)fcl::otlp::install_crash_capture(options), fcl::otlp::exceptions::spool_error);

      auto input = std::ifstream{target, std::ios::binary};
      auto body = std::string{};
      input >> body;
      BOOST_TEST(body == "untouched");
   }

   {
      auto directory = temp_directory{"fcl-otlp-crash-writable-dir"};
      std::filesystem::permissions(directory.path(),
                                   std::filesystem::perms::owner_all | std::filesystem::perms::group_write,
                                   std::filesystem::perm_options::replace);

      auto options = make_spool_options(directory.path());
      options.capture_signals = false;
      BOOST_CHECK_THROW((void)fcl::otlp::install_crash_capture(options), fcl::otlp::exceptions::spool_error);

      std::filesystem::permissions(directory.path(), std::filesystem::perms::owner_all,
                                   std::filesystem::perm_options::replace);
   }
}

BOOST_AUTO_TEST_CASE(crash_spool_creates_private_regular_file) {
   auto directory = temp_directory{"fcl-otlp-crash-private-file"};
   auto options = make_spool_options(directory.path());
   options.capture_terminate = false;
   auto guard = fcl::otlp::install_crash_capture(options);

   const auto path = directory.path() / ("crash-" + std::to_string(::getpid()) + ".spool");
   BOOST_REQUIRE(std::filesystem::exists(path));

   struct stat stat_value {};
   BOOST_REQUIRE(::lstat(path.c_str(), &stat_value) == 0);
   BOOST_TEST(S_ISREG(stat_value.st_mode));
   BOOST_TEST(stat_value.st_uid == ::geteuid());
   BOOST_TEST((stat_value.st_mode & (S_IWGRP | S_IWOTH)) == 0);
}

BOOST_AUTO_TEST_CASE(crash_spool_reuses_existing_safe_file) {
   auto directory = temp_directory{"fcl-otlp-crash-reuse"};
   auto options = make_spool_options(directory.path());

   {
      auto guard = fcl::otlp::install_crash_capture(options);
      BOOST_REQUIRE(guard);
      BOOST_CHECK_THROW((void)fcl::otlp::install_crash_capture(options), fcl::otlp::exceptions::capture_active);
   }

   const auto path = directory.path() / ("crash-" + std::to_string(::getpid()) + ".spool");
   BOOST_REQUIRE(std::filesystem::exists(path));
   auto guard = fcl::otlp::install_crash_capture(options);
   BOOST_REQUIRE(guard);

   struct stat stat_value {};
   BOOST_REQUIRE(::lstat(path.c_str(), &stat_value) == 0);
   BOOST_TEST(S_ISREG(stat_value.st_mode));
   BOOST_TEST(stat_value.st_uid == ::geteuid());
   BOOST_TEST((stat_value.st_mode & (S_IWGRP | S_IWOTH)) == 0);
}

BOOST_AUTO_TEST_CASE(crash_resend_skips_active_current_process_spool) {
   auto directory = temp_directory{"fcl-otlp-crash-active-resend"};
   auto options = make_spool_options(directory.path());
   options.capture_terminate = false;
   auto guard = fcl::otlp::install_crash_capture(options);
   BOOST_REQUIRE(guard);

   const auto path = directory.path() / ("crash-" + std::to_string(::getpid()) + ".spool");
   BOOST_REQUIRE(std::filesystem::exists(path));

   auto runtime = fcl::asio::runtime{};
   auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}};
   auto exporter = fcl::otlp::log_exporter{runtime, make_options(collector)};
   const auto result =
       fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, make_spool_options(directory.path())));
   fcl::asio::blocking::run(runtime, exporter.async_shutdown());

   BOOST_TEST(result.files_scanned == 1U);
   BOOST_TEST(result.files_retained == 1U);
   BOOST_TEST(result.bad_files == 0U);
   BOOST_TEST(result.exported_records == 0U);
   BOOST_TEST(collector.requests().empty());
   BOOST_TEST(std::filesystem::exists(path));
   BOOST_TEST(!std::filesystem::exists(path.string() + ".bad"));

   struct stat stat_value {};
   BOOST_REQUIRE(::lstat(path.c_str(), &stat_value) == 0);
   BOOST_TEST(S_ISREG(stat_value.st_mode));
   BOOST_TEST(stat_value.st_uid == ::geteuid());
   BOOST_TEST((stat_value.st_mode & (S_IWGRP | S_IWOTH)) == 0);
}

BOOST_AUTO_TEST_CASE(crash_resend_does_not_mutate_spool_when_capture_installs_concurrently) {
   auto source_directory = temp_directory{"fcl-otlp-crash-race-source"};
   BOOST_TEST(run_crash_helper("sigabrt", source_directory.path()) == 0);
   const auto source = first_spool_file(source_directory.path());

   auto directory = temp_directory{"fcl-otlp-crash-race"};
   const auto active_path = directory.path() / ("crash-" + std::to_string(::getpid()) + ".spool");
   std::filesystem::copy_file(source, active_path);
   std::filesystem::permissions(active_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                std::filesystem::perm_options::replace);

   auto runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}, true};
   auto exporter = fcl::otlp::log_exporter{runtime, make_options(collector)};

   auto result = fcl::otlp::crash_resend_result{};
   auto error = std::exception_ptr{};
   auto resend = std::thread{[&] {
      try {
         result =
             fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, make_spool_options(directory.path())));
      } catch (...) {
         error = std::current_exception();
      }
   }};

   if (!collector.wait_for_requests(1)) {
      collector.release_responses();
      resend.join();
      BOOST_FAIL("expected resend to reach the fake OTLP collector before installing crash capture");
   }

   auto options = make_spool_options(directory.path());
   options.capture_terminate = false;
   auto guard = fcl::otlp::install_crash_capture(options);
   BOOST_REQUIRE(guard);
   BOOST_CHECK_THROW((void)fcl::otlp::install_crash_capture(options), fcl::otlp::exceptions::capture_active);

   collector.release_responses();
   resend.join();
   if (error) {
      std::rethrow_exception(error);
   }
   fcl::asio::blocking::run(runtime, exporter.async_shutdown());

   BOOST_TEST(result.records_read == 1U);
   BOOST_TEST(result.exported_records == 1U);
   BOOST_TEST(result.files_retained == 1U);
   BOOST_TEST(std::filesystem::exists(active_path));
   BOOST_TEST(!std::filesystem::exists(active_path.string() + ".bad"));
   BOOST_TEST(count_spool_files(directory.path()) == 1U);

   struct stat stat_value {};
   BOOST_REQUIRE(::lstat(active_path.c_str(), &stat_value) == 0);
   BOOST_TEST(S_ISREG(stat_value.st_mode));
   BOOST_TEST(stat_value.st_uid == ::geteuid());
   BOOST_TEST((stat_value.st_mode & (S_IWGRP | S_IWOTH)) == 0);
}

BOOST_AUTO_TEST_CASE(crash_resend_does_not_drop_retained_records_when_resends_overlap) {
   auto first_source_directory = temp_directory{"fcl-otlp-crash-overlap-first"};
   auto second_source_directory = temp_directory{"fcl-otlp-crash-overlap-second"};
   BOOST_TEST(run_crash_helper("sigabrt", first_source_directory.path()) == 0);
   BOOST_TEST(run_crash_helper("sigabrt", second_source_directory.path()) == 0);
   const auto first_source = first_spool_file(first_source_directory.path());
   const auto second_source = first_spool_file(second_source_directory.path());
   const auto record_size = std::filesystem::file_size(first_source);
   BOOST_REQUIRE(record_size > 0);
   BOOST_REQUIRE_EQUAL(std::filesystem::file_size(second_source), record_size);

   auto directory = temp_directory{"fcl-otlp-crash-overlap"};
   const auto target = directory.path() / "crash-999999.spool";
   std::filesystem::copy_file(first_source, target);
   append_file_bytes(second_source, target);
   std::filesystem::permissions(target, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                std::filesystem::perm_options::replace);
   BOOST_REQUIRE_EQUAL(std::filesystem::file_size(target), record_size * 2);

   auto first_collector_runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto second_collector_runtime = fcl::asio::runtime{fcl::asio::runtime_options{.worker_threads = 2}};
   auto first_collector = fake_collector{first_collector_runtime, {{.status = fcl::http::status::ok}}, true};
   auto second_collector = fake_collector{second_collector_runtime, {{.status = fcl::http::status::ok}}, true};
   auto first_runtime = fcl::asio::runtime{};
   auto second_runtime = fcl::asio::runtime{};
   auto first_exporter = fcl::otlp::log_exporter{first_runtime, make_options(first_collector)};
   auto second_exporter = fcl::otlp::log_exporter{second_runtime, make_options(second_collector)};
   auto first_options = make_spool_options(directory.path());
   auto second_options = make_spool_options(directory.path());
   first_options.max_records_per_resend = 1;
   second_options.max_records_per_resend = 1;

   auto first_result = fcl::otlp::crash_resend_result{};
   auto second_result = fcl::otlp::crash_resend_result{};
   auto first_error = std::exception_ptr{};
   auto second_error = std::exception_ptr{};
   auto first_resend = std::thread{[&] {
      try {
         first_result = fcl::asio::blocking::run(first_runtime,
                                                 fcl::otlp::async_resend_crashes(first_exporter, first_options));
      } catch (...) {
         first_error = std::current_exception();
      }
   }};
   auto second_resend = std::thread{[&] {
      try {
         second_result = fcl::asio::blocking::run(second_runtime,
                                                  fcl::otlp::async_resend_crashes(second_exporter, second_options));
      } catch (...) {
         second_error = std::current_exception();
      }
   }};

   if (!first_collector.wait_for_requests(1) || !second_collector.wait_for_requests(1)) {
      first_collector.release_responses();
      second_collector.release_responses();
      first_resend.join();
      second_resend.join();
      BOOST_FAIL("expected both resend operations to export before release");
   }

   first_collector.release_responses();
   second_collector.release_responses();
   first_resend.join();
   second_resend.join();
   if (first_error) {
      std::rethrow_exception(first_error);
   }
   if (second_error) {
      std::rethrow_exception(second_error);
   }
   fcl::asio::blocking::run(first_runtime, first_exporter.async_shutdown());
   fcl::asio::blocking::run(second_runtime, second_exporter.async_shutdown());

   BOOST_TEST(first_result.exported_records + second_result.exported_records == 2U);
   BOOST_TEST(first_result.files_retained + second_result.files_retained >= 1U);
   BOOST_REQUIRE(std::filesystem::exists(target));
   BOOST_TEST(std::filesystem::file_size(target) == record_size);

   struct stat stat_value {};
   BOOST_REQUIRE(::lstat(target.c_str(), &stat_value) == 0);
   BOOST_TEST(S_ISREG(stat_value.st_mode));
   BOOST_TEST(stat_value.st_uid == ::geteuid());
   BOOST_TEST((stat_value.st_mode & (S_IWGRP | S_IWOTH)) == 0);

   auto followup_runtime = fcl::asio::runtime{};
   auto followup_collector = fake_collector{followup_runtime, {{.status = fcl::http::status::ok}}};
   auto followup_exporter = fcl::otlp::log_exporter{followup_runtime, make_options(followup_collector)};
   const auto followup_result =
       fcl::asio::blocking::run(followup_runtime,
                                fcl::otlp::async_resend_crashes(followup_exporter, make_spool_options(directory.path())));
   fcl::asio::blocking::run(followup_runtime, followup_exporter.async_shutdown());

   BOOST_REQUIRE(followup_collector.wait_for_requests(1));
   BOOST_TEST(followup_result.records_read == 1U);
   BOOST_TEST(followup_result.exported_records == 1U);
   BOOST_TEST(count_spool_files(directory.path()) == 0U);
}
#endif

#if defined(__unix__) || defined(__APPLE__)
BOOST_AUTO_TEST_CASE(crash_resend_rejects_unsafe_directory) {
   auto directory = temp_directory{"fcl-otlp-crash-resend-writable-dir"};
   std::filesystem::permissions(directory.path(), std::filesystem::perms::owner_all | std::filesystem::perms::group_write,
                                std::filesystem::perm_options::replace);

   auto runtime = fcl::asio::runtime{};
   auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}};
   auto exporter = fcl::otlp::log_exporter{runtime, make_options(collector)};

   BOOST_CHECK_THROW(
       (void)fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, make_spool_options(directory.path()))),
       fcl::otlp::exceptions::spool_error);
   BOOST_TEST(collector.requests().empty());

   std::filesystem::permissions(directory.path(), std::filesystem::perms::owner_all,
                                std::filesystem::perm_options::replace);
}

BOOST_AUTO_TEST_CASE(crash_resend_does_not_follow_spool_symlink) {
   auto target_directory = temp_directory{"fcl-otlp-crash-resend-target"};
   BOOST_TEST(run_crash_helper("sigabrt", target_directory.path()) == 0);
   const auto target = first_spool_file(target_directory.path());
   const auto target_size = std::filesystem::file_size(target);

   auto directory = temp_directory{"fcl-otlp-crash-resend-symlink"};
   const auto link = directory.path() / "crash-999999.spool";
   std::filesystem::create_symlink(target, link);

   auto runtime = fcl::asio::runtime{};
   auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}};
   auto exporter = fcl::otlp::log_exporter{runtime, make_options(collector)};
   const auto result =
       fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, make_spool_options(directory.path())));
   fcl::asio::blocking::run(runtime, exporter.async_shutdown());

   BOOST_TEST(result.bad_files == 1U);
   BOOST_TEST(result.exported_records == 0U);
   BOOST_TEST(collector.requests().empty());
   BOOST_TEST(std::filesystem::exists(target));
   BOOST_TEST(std::filesystem::file_size(target) == target_size);
   BOOST_TEST(std::filesystem::exists(directory.path() / "crash-999999.spool.bad"));
}
#endif

BOOST_AUTO_TEST_CASE(next_start_resends_terminate_spool_as_safe_fatal_log) {
   auto directory = temp_directory{"fcl-otlp-crash-terminate"};
   BOOST_TEST(run_crash_helper("terminate", directory.path()) == 0);
   BOOST_REQUIRE_EQUAL(count_spool_files(directory.path()), 1U);

   auto runtime = fcl::asio::runtime{};
   auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}};
   auto exporter = fcl::otlp::log_exporter{runtime, make_options(collector)};

   const auto result = fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, make_spool_options(directory.path())));
   fcl::asio::blocking::run(runtime, exporter.async_shutdown());

   BOOST_REQUIRE(collector.wait_for_requests(1));
   BOOST_TEST(result.records_read == 1U);
   BOOST_TEST(result.exported_records == 1U);
   BOOST_TEST(result.failed_records == 0U);
   BOOST_TEST(count_spool_files(directory.path()) == 0U);

   const auto body = collector.requests().front().body;
   expect_contains(body, "\"severityText\":\"ERROR\"");
   expect_contains(body, "fcl crash captured");
   expect_contains(body, "crash.severity");
   expect_contains(body, "fatal");
   expect_contains(body, "crash.kind");
   expect_contains(body, "terminate");
   expect_contains(body, "exception.category");
   expect_contains(body, "fcl.otlp.test");
   expect_contains(body, "exception.code");
   expect_not_contains(body, "super-secret-token");
}

BOOST_AUTO_TEST_CASE(next_start_resends_signal_spool) {
   auto directory = temp_directory{"fcl-otlp-crash-signal"};
   BOOST_TEST(run_crash_helper("sigabrt", directory.path()) == 0);
   BOOST_REQUIRE_EQUAL(count_spool_files(directory.path()), 1U);

   auto runtime = fcl::asio::runtime{};
   auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}};
   auto exporter = fcl::otlp::log_exporter{runtime, make_options(collector)};

   const auto result = fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, make_spool_options(directory.path())));
   fcl::asio::blocking::run(runtime, exporter.async_shutdown());

   BOOST_REQUIRE(collector.wait_for_requests(1));
   BOOST_TEST(result.records_read == 1U);
   BOOST_TEST(result.exported_records == 1U);
   BOOST_TEST(count_spool_files(directory.path()) == 0U);

   const auto body = collector.requests().front().body;
   expect_contains(body, "crash.kind");
   expect_contains(body, "signal");
   expect_contains(body, "signal.number");
}

BOOST_AUTO_TEST_CASE(permanent_export_failure_leaves_spool_for_retry) {
   auto directory = temp_directory{"fcl-otlp-crash-retry"};
   BOOST_TEST(run_crash_helper("sigabrt", directory.path()) == 0);
   BOOST_REQUIRE_EQUAL(count_spool_files(directory.path()), 1U);

   {
      auto runtime = fcl::asio::runtime{};
      auto collector = fake_collector{runtime, {{.status = fcl::http::status::bad_request}}};
      auto exporter = fcl::otlp::log_exporter{runtime, make_options(collector)};
      const auto result =
          fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, make_spool_options(directory.path())));
      fcl::asio::blocking::run(runtime, exporter.async_shutdown());

      BOOST_REQUIRE(collector.wait_for_requests(1));
      BOOST_TEST(result.exported_records == 0U);
      BOOST_TEST(result.failed_records == 1U);
      BOOST_TEST(count_spool_files(directory.path()) == 1U);
   }

   {
      auto runtime = fcl::asio::runtime{};
      auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}};
      auto exporter = fcl::otlp::log_exporter{runtime, make_options(collector)};
      const auto result =
          fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, make_spool_options(directory.path())));
      fcl::asio::blocking::run(runtime, exporter.async_shutdown());

      BOOST_REQUIRE(collector.wait_for_requests(1));
      BOOST_TEST(result.exported_records == 1U);
      BOOST_TEST(result.failed_records == 0U);
      BOOST_TEST(count_spool_files(directory.path()) == 0U);
   }
}

BOOST_AUTO_TEST_CASE(malformed_spool_is_quarantined_and_resend_is_bounded) {
   {
      auto directory = temp_directory{"fcl-otlp-crash-bad"};
      auto file = std::ofstream{directory.path() / "crash-1.spool", std::ios::binary};
      file << "truncated";
      file.close();

      auto runtime = fcl::asio::runtime{};
      auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}};
      auto exporter = fcl::otlp::log_exporter{runtime, make_options(collector)};
      const auto result =
          fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, make_spool_options(directory.path())));
      fcl::asio::blocking::run(runtime, exporter.async_shutdown());

      BOOST_TEST(result.bad_files == 1U);
      BOOST_TEST(result.records_read == 0U);
      BOOST_TEST(count_spool_files(directory.path()) == 0U);
      BOOST_TEST(std::filesystem::exists(directory.path() / "crash-1.spool.bad"));
      BOOST_TEST(collector.requests().empty());
   }

   {
      auto directory = temp_directory{"fcl-otlp-crash-bounded"};
      BOOST_TEST(run_crash_helper("sigabrt", directory.path()) == 0);
      BOOST_TEST(run_crash_helper("sigabrt", directory.path()) == 0);
      BOOST_REQUIRE_EQUAL(count_spool_files(directory.path()), 2U);

      auto options = make_spool_options(directory.path());
      options.max_records_per_resend = 1;

      auto runtime = fcl::asio::runtime{};
      auto collector = fake_collector{runtime, {{.status = fcl::http::status::ok}}};
      auto exporter = fcl::otlp::log_exporter{runtime, make_options(collector)};
      const auto result = fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, options));
      fcl::asio::blocking::run(runtime, exporter.async_shutdown());

      BOOST_REQUIRE(collector.wait_for_requests(1));
      BOOST_TEST(result.records_read == 1U);
      BOOST_TEST(result.exported_records == 1U);
      BOOST_TEST(count_spool_files(directory.path()) == 1U);
   }
}

BOOST_AUTO_TEST_SUITE_END()
