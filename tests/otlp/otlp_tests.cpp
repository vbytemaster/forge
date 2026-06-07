#include <boost/test/unit_test.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.http.route_context;
import fcl.http.server;
import fcl.http.types;
import fcl.log.log_message;
import fcl.log.record;
import fcl.otlp;

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
   fake_collector(fcl::asio::runtime& runtime, std::vector<collector_response> responses)
       : responses_(std::move(responses)),
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
      server_.stop();
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

      auto response = fcl::http::make_text_response(context.request, response_value.status, response_value.body,
                                                    "application/json");
      if (response_value.retry_after.has_value()) {
         response.set(fcl::http::field::retry_after, *response_value.retry_after);
      }
      return response;
   }

   std::vector<collector_response> responses_;
   fcl::http::server server_;
   mutable std::mutex mutex_;
   mutable std::condition_variable ready_;
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

BOOST_AUTO_TEST_SUITE_END()
