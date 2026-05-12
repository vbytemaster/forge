#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fcl_json_tests {

struct http_config {
   std::uint16_t bind_port = 0;
   std::string bind_host;
   bool tls_enabled = false;
   std::vector<std::string> tags;
};

} // namespace fcl_json_tests

BOOST_DESCRIBE_STRUCT(fcl_json_tests::http_config, (), (bind_port, bind_host, tls_enabled, tags))

import fcl.config;
import fcl.json;
import fcl.schema;
import fcl.variant;

template <>
struct fcl::schema::rules<fcl_json_tests::http_config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl_json_tests::http_config> define() {
      auto schema = fcl::schema::object<fcl_json_tests::http_config>();
      schema.field<&fcl_json_tests::http_config::bind_port>("bind-port").required().default_value(8080).range(1, 65535);
      schema.field<&fcl_json_tests::http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&fcl_json_tests::http_config::tls_enabled>("tls-enabled").default_value(false);
      static_cast<void>(schema.field<&fcl_json_tests::http_config::tags>("tags"));
      return schema;
   }
};

BOOST_AUTO_TEST_SUITE(json_codec_tests)

BOOST_AUTO_TEST_CASE(json_value_roundtrip_preserves_generic_shapes) {
   const auto parsed = fcl::json::read_value(R"({"null":null,"flag":true,"i":-2,"u":7,"d":3.5,"s":"x","a":[1,"b"]})");
   BOOST_REQUIRE(parsed.ok());

   const auto& object = parsed.value.get_object();
   BOOST_TEST(object["flag"].as_bool());
   BOOST_TEST(object["i"].as_int64() == -2);
   BOOST_TEST(object["u"].as_uint64() == 7U);
   BOOST_TEST(object["d"].as_double() == 3.5);
   BOOST_TEST(object["s"].get_string() == "x");
   BOOST_REQUIRE_EQUAL(object["a"].get_array().size(), 2U);

   const auto written = fcl::json::write_value(parsed.value);
   BOOST_REQUIRE(written.ok());
   const auto reparsed = fcl::json::read_value(written.text);
   BOOST_REQUIRE(reparsed.ok());
   BOOST_TEST(reparsed.value.get_object()["flag"].as_bool());
   BOOST_TEST(reparsed.value.get_object()["i"].as_int64() == -2);
   BOOST_TEST(reparsed.value.get_object()["u"].as_uint64() == 7U);
   BOOST_REQUIRE_EQUAL(reparsed.value.get_object()["a"].get_array().size(), 2U);
}

BOOST_AUTO_TEST_CASE(json_large_uint64_is_not_silently_converted_to_double) {
   const auto parsed = fcl::json::read_value(R"({"max":18446744073709551615})");
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.get_object()["max"].as_uint64() == 18446744073709551615ULL);

   const auto written = fcl::json::write_value(parsed.value);
   BOOST_REQUIRE(written.ok());
   BOOST_TEST(written.text.find("18446744073709551615") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(json_document_roundtrip_uses_config_document) {
   auto document = fcl::config::document{};
   document.set("http.bind-host", "127.0.0.1");
   document.set("http.bind-port", 8080);
   document.set("http.tags", fcl::config::value::array_type{fcl::config::value{"alpha"}, fcl::config::value{"beta"}});

   const auto written = fcl::json::write_document(document, {.pretty = true});
   BOOST_REQUIRE(written.ok());
   const auto parsed = fcl::json::read_document(written.text);
   BOOST_REQUIRE(parsed.ok());
   BOOST_REQUIRE(parsed.value.try_get("http.bind-host") != nullptr);
   BOOST_REQUIRE(parsed.value.try_get("http.bind-port") != nullptr);
   BOOST_REQUIRE(parsed.value.try_get("http.tags") != nullptr);
}

BOOST_AUTO_TEST_CASE(json_typed_read_uses_schema_defaults_validation_and_unknown_policy) {
   const auto parsed = fcl::json::read<fcl_json_tests::http_config>(
      R"({"bind-port":9090,"tls-enabled":false,"tags":["alpha"],"extra":1})");
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.bind_port == 9090U);
   BOOST_TEST(parsed.value.bind_host == "127.0.0.1");
   BOOST_REQUIRE_EQUAL(parsed.value.tags.size(), 1U);
   BOOST_TEST(parsed.diagnostics.size() == 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "json.unknown");

   auto options = fcl::json::read_options{};
   options.unknown_fields = fcl::json::unknown_field_policy::error;
   const auto rejected = fcl::json::read<fcl_json_tests::http_config>(
      R"({"bind-port":9090,"extra":1})",
      options);
   BOOST_TEST(!rejected.ok());
   BOOST_TEST(rejected.diagnostics.front().code == "json.unknown");

   const auto invalid = fcl::json::read<fcl_json_tests::http_config>(R"({"bind-port":0})");
   BOOST_TEST(!invalid.ok());
}

BOOST_AUTO_TEST_CASE(json_typed_load_uses_same_unknown_policy_as_read) {
   const auto path = std::filesystem::temp_directory_path() /
                     ("fcl_json_unknown_policy_" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
   {
      auto out = std::ofstream{path};
      out << R"({"bind-port":9090,"extra":1})";
   }
   struct cleanup {
      std::filesystem::path path;
      ~cleanup() {
         std::error_code ignored;
         std::filesystem::remove(path, ignored);
      }
   } remove_file{path};

   const auto warned = fcl::json::load<fcl_json_tests::http_config>(path);
   BOOST_REQUIRE(warned.ok());
   BOOST_REQUIRE_EQUAL(warned.diagnostics.size(), 1U);
   BOOST_TEST(warned.diagnostics.front().code == "json.unknown");

   auto rejected_options = fcl::json::read_options{};
   rejected_options.unknown_fields = fcl::json::unknown_field_policy::error;
   const auto rejected = fcl::json::load<fcl_json_tests::http_config>(path, rejected_options);
   BOOST_TEST(!rejected.ok());
   BOOST_REQUIRE_EQUAL(rejected.diagnostics.size(), 1U);
   BOOST_TEST(rejected.diagnostics.front().code == "json.unknown");

   auto ignored_options = fcl::json::read_options{};
   ignored_options.unknown_fields = fcl::json::unknown_field_policy::ignore;
   const auto ignored = fcl::json::load<fcl_json_tests::http_config>(path, ignored_options);
   BOOST_REQUIRE(ignored.ok());
   BOOST_TEST(ignored.diagnostics.empty());
   BOOST_TEST(ignored.value.bind_port == 9090U);
}

BOOST_AUTO_TEST_CASE(json_malformed_input_returns_fcl_diagnostic) {
   const auto parsed = fcl::json::read_value(R"({"unterminated":)");
   BOOST_TEST(!parsed.ok());
   BOOST_REQUIRE_EQUAL(parsed.diagnostics.size(), 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "json.parse");
   BOOST_TEST(parsed.diagnostics.front().message.find("glz::") == std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
