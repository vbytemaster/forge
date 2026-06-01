#include <boost/test/unit_test.hpp>
#include <fcl/exceptions/macros.hpp>
#include <fcl/log/macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import fcl.exceptions;
import fcl.log.log_message;
import fcl.log.logger;
import fcl.log.record;

namespace {

class capture_sink final : public fcl::sink {
 public:
   void log(const fcl::log_record& record) override {
      records.push_back(record);
   }

   std::vector<fcl::log_record> records;
};

std::string read_file(const std::filesystem::path& path) {
   auto input = std::ifstream{path};
   return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

BOOST_AUTO_TEST_SUITE(log_test_suite)

BOOST_AUTO_TEST_CASE(disabled_level_does_not_build_record) {
   auto logger = fcl::logger{"test.disabled"};
   logger.set_log_level(fcl::log_level::error);
   auto sink = std::make_shared<capture_sink>();
   logger.add_sink(sink);

   bool evaluated = false;
   fcl_log(logger, fcl::log_level::debug, "hidden", fcl::log_field_provider{[&] {
              evaluated = true;
              return fcl::log_field{"expensive", "value"};
           }});

   BOOST_TEST(!evaluated);
   BOOST_TEST(sink->records.empty());
}

BOOST_AUTO_TEST_CASE(error_record_captures_stacktrace_and_redacts_secrets) {
   auto logger = fcl::logger{"test.error"};
   logger.set_log_level(fcl::log_level::debug);
   auto sink = std::make_shared<capture_sink>();
   logger.add_sink(sink);

   logger.error("failed login", {fcl::log_ctx("user", "alice"), fcl::log_secret("token", "abc123")});

   BOOST_REQUIRE_EQUAL(sink->records.size(), 1U);
   const auto& record = sink->records.front();
   BOOST_TEST(record.message == "failed login");
   BOOST_REQUIRE_EQUAL(record.fields.size(), 2U);
   BOOST_TEST(record.fields[0].value == "alice");
   BOOST_TEST(record.fields[1].value == "<redacted>");
   BOOST_REQUIRE(record.stacktrace.has_value());
   BOOST_TEST(!record.stacktrace->backend.empty());
}

BOOST_AUTO_TEST_CASE(jsonl_sink_writes_redacted_structured_record) {
   const auto path = std::filesystem::temp_directory_path() / "fcl-log-jsonl-test.jsonl";
   std::filesystem::remove(path);

   auto logger = fcl::logger{"test.jsonl"};
   logger.set_log_level(fcl::log_level::debug);
   logger.add_sink(std::make_shared<fcl::jsonl_sink>(path));
   logger.info("configured", {fcl::log_ctx("port", 8080), fcl::log_secret("password", "secret")});

   const auto text = read_file(path);
   BOOST_TEST(text.find("\"logger\":\"test.jsonl\"") != std::string::npos);
   BOOST_TEST(text.find("\"message\":\"configured\"") != std::string::npos);
   BOOST_TEST(text.find("\"port\":\"8080\"") != std::string::npos);
   BOOST_TEST(text.find("\"password\":\"<redacted>\"") != std::string::npos);
   BOOST_TEST(text.find("secret") == std::string::npos);

   std::filesystem::remove(path);
}

BOOST_AUTO_TEST_CASE(exception_chain_can_be_routed_to_logger) {
   auto logger = fcl::logger{"test.exception"};
   logger.set_log_level(fcl::log_level::debug);
   auto sink = std::make_shared<capture_sink>();
   logger.add_sink(sink);

   fcl::exceptions::set_log_sink([&](std::string_view message) {
      logger.error("exception captured", {fcl::log_ctx("chain", message), fcl::log_secret("token", "hidden")});
   });

   try {
      try {
         throw std::runtime_error{"inner"};
      }
      FCL_CAPTURE_AND_LOG("outer", fcl::exceptions::ctx("phase", "startup"), fcl::exceptions::secret("password", "secret"))
   } catch (...) {
      BOOST_FAIL("FCL_CAPTURE_AND_LOG must not rethrow");
   }

   BOOST_REQUIRE_EQUAL(sink->records.size(), 1U);
   const auto& record = sink->records.front();
   BOOST_TEST(record.message == "exception captured");
   BOOST_TEST(record.fields.front().value.find("outer") != std::string::npos);
   BOOST_TEST(record.fields.front().value.find("inner") != std::string::npos);
   BOOST_TEST(record.fields.front().value.find("secret") == std::string::npos);
   BOOST_TEST(record.fields.back().value == "<redacted>");

   fcl::exceptions::set_log_sink({});
}

BOOST_AUTO_TEST_SUITE_END()
