#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fcl_yaml_tests {

struct http_config {
   std::uint16_t bind_port = 0;
   std::string bind_host;
   bool tls_enabled = false;
   std::vector<std::string> tags;
};

} // namespace fcl_yaml_tests

BOOST_DESCRIBE_STRUCT(fcl_yaml_tests::http_config, (), (bind_port, bind_host, tls_enabled, tags))

import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;
import fcl.variant.exceptions;
import fcl.variant.value;
import fcl.variant.conversion;
import fcl.variant.containers;
import fcl.variant.chrono;
import fcl.variant.multiprecision;
import fcl.variant.format;
import fcl.variant.described;
import fcl.yaml;

template <> struct fcl::schema::rules<fcl_yaml_tests::http_config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl_yaml_tests::http_config> define() {
      auto schema = fcl::schema::object<fcl_yaml_tests::http_config>();
      schema.field<&fcl_yaml_tests::http_config::bind_port>("bind-port").required().default_value(8080).range(1, 65535);
      schema.field<&fcl_yaml_tests::http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&fcl_yaml_tests::http_config::tls_enabled>("tls-enabled").default_value(false);
      static_cast<void>(schema.field<&fcl_yaml_tests::http_config::tags>("tags"));
      return schema;
   }
};

BOOST_AUTO_TEST_SUITE(yaml_codec_tests)

BOOST_AUTO_TEST_CASE(yaml_value_roundtrip_preserves_scalars_lists_and_maps) {
   const auto parsed = fcl::yaml::read_value("flag: true\n"
                                             "i: -2\n"
                                             "u: 7\n"
                                             "d: 3.5\n"
                                             "s: x\n"
                                             "a:\n"
                                             "  - 1\n"
                                             "  - b\n");

   BOOST_REQUIRE(parsed.ok());
   const auto& object = parsed.value.get_object();
   BOOST_TEST(object["flag"].as_bool());
   BOOST_TEST(object["i"].as_int64() == -2);
   BOOST_TEST(object["u"].as_uint64() == 7U);
   BOOST_TEST(object["d"].as_double() == 3.5);
   BOOST_TEST(object["s"].get_string() == "x");
   BOOST_REQUIRE_EQUAL(object["a"].get_array().size(), 2U);

   const auto written = fcl::yaml::write_value(parsed.value);
   BOOST_REQUIRE(written.ok());
   const auto reparsed = fcl::yaml::read_value(written.text);
   BOOST_REQUIRE(reparsed.ok());
   BOOST_TEST(reparsed.value.get_object()["flag"].as_bool());
   BOOST_TEST(reparsed.value.get_object()["i"].as_int64() == -2);
   BOOST_TEST(reparsed.value.get_object()["u"].as_uint64() == 7U);
   BOOST_REQUIRE_EQUAL(reparsed.value.get_object()["a"].get_array().size(), 2U);
}

BOOST_AUTO_TEST_CASE(yaml_document_roundtrip_uses_config_document) {
   auto document = fcl::config::document{};
   document.set("http.bind-host", "127.0.0.1");
   document.set("http.bind-port", 8080);
   document.set("http.tls-enabled", true);

   const auto written = fcl::yaml::write_document(document);
   BOOST_REQUIRE(written.ok());
   const auto parsed = fcl::yaml::read_document(written.text);
   BOOST_REQUIRE(parsed.ok());
   BOOST_REQUIRE(parsed.value.try_get("http.bind-host") != nullptr);
   BOOST_REQUIRE(parsed.value.try_get("http.bind-port") != nullptr);
}

BOOST_AUTO_TEST_CASE(yaml_typed_read_uses_schema_defaults_validation_and_unknown_policy) {
   const auto parsed = fcl::yaml::read<fcl_yaml_tests::http_config>("bind-port: 9090\n"
                                                                    "tls-enabled: false\n"
                                                                    "tags:\n"
                                                                    "  - alpha\n"
                                                                    "extra: 1\n");
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.bind_port == 9090U);
   BOOST_TEST(parsed.value.bind_host == "127.0.0.1");
   BOOST_REQUIRE_EQUAL(parsed.value.tags.size(), 1U);
   BOOST_TEST(parsed.diagnostics.size() == 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "yaml.unknown");

   auto options = fcl::yaml::read_options{};
   options.unknown_fields = fcl::yaml::unknown_field_policy::error;
   const auto rejected = fcl::yaml::read<fcl_yaml_tests::http_config>("bind-port: 9090\n"
                                                                      "extra: 1\n",
                                                                      options);
   BOOST_TEST(!rejected.ok());

   const auto invalid = fcl::yaml::read<fcl_yaml_tests::http_config>("bind-port: 0\n");
   BOOST_TEST(!invalid.ok());
}

BOOST_AUTO_TEST_CASE(yaml_typed_load_uses_same_unknown_policy_as_read) {
   const auto path = std::filesystem::temp_directory_path() /
                     ("fcl_yaml_unknown_policy_" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".yaml");
   {
      auto out = std::ofstream{path};
      out << "bind-port: 9090\nextra: 1\n";
   }
   struct cleanup {
      std::filesystem::path path;
      ~cleanup() {
         std::error_code ignored;
         std::filesystem::remove(path, ignored);
      }
   } remove_file{path};

   const auto warned = fcl::yaml::load<fcl_yaml_tests::http_config>(path);
   BOOST_REQUIRE(warned.ok());
   BOOST_REQUIRE_EQUAL(warned.diagnostics.size(), 1U);
   BOOST_TEST(warned.diagnostics.front().code == "yaml.unknown");

   auto rejected_options = fcl::yaml::read_options{};
   rejected_options.unknown_fields = fcl::yaml::unknown_field_policy::error;
   const auto rejected = fcl::yaml::load<fcl_yaml_tests::http_config>(path, rejected_options);
   BOOST_TEST(!rejected.ok());
   BOOST_REQUIRE_EQUAL(rejected.diagnostics.size(), 1U);
   BOOST_TEST(rejected.diagnostics.front().code == "yaml.unknown");

   auto ignored_options = fcl::yaml::read_options{};
   ignored_options.unknown_fields = fcl::yaml::unknown_field_policy::ignore;
   const auto ignored = fcl::yaml::load<fcl_yaml_tests::http_config>(path, ignored_options);
   BOOST_REQUIRE(ignored.ok());
   BOOST_TEST(ignored.diagnostics.empty());
   BOOST_TEST(ignored.value.bind_port == 9090U);
}

BOOST_AUTO_TEST_CASE(yaml_malformed_input_returns_fcl_diagnostic) {
   const auto parsed = fcl::yaml::read_value("root: [unterminated\n");
   BOOST_TEST(!parsed.ok());
   BOOST_REQUIRE_EQUAL(parsed.diagnostics.size(), 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "yaml.parse");
   BOOST_TEST(parsed.diagnostics.front().message.find("glz::") == std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
