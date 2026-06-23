#include <boost/test/unit_test.hpp>
#include <forge/exceptions/macros.hpp>
#include <forge/log/macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import forge.exceptions;
import forge.log.log_message;
import forge.log.logger;
import forge.log.record;

namespace {

class capture_sink final : public forge::sink {
 public:
   void log(const forge::log_record& record) override {
      records.push_back(record);
   }

   std::vector<forge::log_record> records;
};

std::string read_file(const std::filesystem::path& path) {
   auto input = std::ifstream{path};
   return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

BOOST_AUTO_TEST_SUITE(log_test_suite)

BOOST_AUTO_TEST_CASE(disabled_level_does_not_build_record) {
   auto logger = forge::logger{"test.disabled"};
   logger.set_log_level(forge::log_level::error);
   auto sink = std::make_shared<capture_sink>();
   logger.add_sink(sink);

   bool evaluated = false;
   forge_log(logger, forge::log_level::debug, "hidden", forge::log_field_provider{[&] {
              evaluated = true;
              return forge::log_field{"expensive", "value"};
           }});

   BOOST_TEST(!evaluated);
   BOOST_TEST(sink->records.empty());
}

BOOST_AUTO_TEST_CASE(error_record_captures_stacktrace_and_redacts_secrets) {
   auto logger = forge::logger{"test.error"};
   logger.set_log_level(forge::log_level::debug);
   auto sink = std::make_shared<capture_sink>();
   logger.add_sink(sink);

   logger.error("failed login", {forge::log_ctx("user", "alice"), forge::log_secret("token", "abc123")});

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
   const auto path = std::filesystem::temp_directory_path() / "forge-log-jsonl-test.jsonl";
   std::filesystem::remove(path);

   auto logger = forge::logger{"test.jsonl"};
   logger.set_log_level(forge::log_level::debug);
   logger.add_sink(std::make_shared<forge::jsonl_sink>(path));
   logger.info("configured", {forge::log_ctx("port", 8080), forge::log_secret("password", "secret")});

   const auto text = read_file(path);
   BOOST_TEST(text.find("\"logger\":\"test.jsonl\"") != std::string::npos);
   BOOST_TEST(text.find("\"message\":\"configured\"") != std::string::npos);
   BOOST_TEST(text.find("\"port\":\"8080\"") != std::string::npos);
   BOOST_TEST(text.find("\"password\":\"<redacted>\"") != std::string::npos);
   BOOST_TEST(text.find("secret") == std::string::npos);

   std::filesystem::remove(path);
}

BOOST_AUTO_TEST_CASE(exception_chain_can_be_routed_to_logger) {
   auto logger = forge::logger{"test.exception"};
   logger.set_log_level(forge::log_level::debug);
   auto sink = std::make_shared<capture_sink>();
   logger.add_sink(sink);

   forge::exceptions::set_log_sink([&](std::string_view message) {
      logger.error("exception captured", {forge::log_ctx("chain", message), forge::log_secret("token", "hidden")});
   });

   try {
      try {
         throw std::runtime_error{"inner"};
      }
      FORGE_CAPTURE_AND_LOG("outer", forge::exceptions::ctx("phase", "startup"), forge::exceptions::secret("password", "secret"))
   } catch (...) {
      BOOST_FAIL("FORGE_CAPTURE_AND_LOG must not rethrow");
   }

   BOOST_REQUIRE_EQUAL(sink->records.size(), 1U);
   const auto& record = sink->records.front();
   BOOST_TEST(record.message == "exception captured");
   BOOST_TEST(record.fields.front().value.find("outer") != std::string::npos);
   BOOST_TEST(record.fields.front().value.find("inner") != std::string::npos);
   BOOST_TEST(record.fields.front().value.find("secret") == std::string::npos);
   BOOST_TEST(record.fields.back().value == "<redacted>");

   forge::exceptions::set_log_sink({});
}

BOOST_AUTO_TEST_SUITE_END()
