#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace forge_yaml_tests {

struct http_config {
   std::uint16_t bind_port = 0;
   std::string bind_host;
   bool tls_enabled = false;
   std::vector<std::string> tags;
};

} // namespace forge_yaml_tests

BOOST_DESCRIBE_STRUCT(forge_yaml_tests::http_config, (), (bind_port, bind_host, tls_enabled, tags))

import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;
import forge.yaml;

template <> struct forge::schema::rules<forge_yaml_tests::http_config> {
   [[nodiscard]] static forge::schema::object_schema<forge_yaml_tests::http_config> define() {
      auto schema = forge::schema::object<forge_yaml_tests::http_config>();
      schema.field<&forge_yaml_tests::http_config::bind_port>("bind-port").required().default_value(8080).range(1, 65535);
      schema.field<&forge_yaml_tests::http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&forge_yaml_tests::http_config::tls_enabled>("tls-enabled").default_value(false);
      static_cast<void>(schema.field<&forge_yaml_tests::http_config::tags>("tags"));
      return schema;
   }
};

BOOST_AUTO_TEST_SUITE(yaml_codec_tests)

BOOST_AUTO_TEST_CASE(yaml_value_roundtrip_preserves_scalars_lists_and_maps) {
   const auto parsed = forge::yaml::read_value("flag: true\n"
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

   const auto written = forge::yaml::write_value(parsed.value);
   BOOST_REQUIRE(written.ok());
   const auto reparsed = forge::yaml::read_value(written.text);
   BOOST_REQUIRE(reparsed.ok());
   BOOST_TEST(reparsed.value.get_object()["flag"].as_bool());
   BOOST_TEST(reparsed.value.get_object()["i"].as_int64() == -2);
   BOOST_TEST(reparsed.value.get_object()["u"].as_uint64() == 7U);
   BOOST_REQUIRE_EQUAL(reparsed.value.get_object()["a"].get_array().size(), 2U);
}

BOOST_AUTO_TEST_CASE(yaml_document_roundtrip_uses_config_document) {
   auto document = forge::config::document{};
   document.set("http.bind-host", "127.0.0.1");
   document.set("http.bind-port", 8080);
   document.set("http.tls-enabled", true);

   const auto written = forge::yaml::write_document(document);
   BOOST_REQUIRE(written.ok());
   const auto parsed = forge::yaml::read_document(written.text);
   BOOST_REQUIRE(parsed.ok());
   BOOST_REQUIRE(parsed.value.try_get("http.bind-host") != nullptr);
   BOOST_REQUIRE(parsed.value.try_get("http.bind-port") != nullptr);
}

BOOST_AUTO_TEST_CASE(yaml_typed_read_uses_schema_defaults_validation_and_unknown_policy) {
   const auto parsed = forge::yaml::read<forge_yaml_tests::http_config>("bind-port: 9090\n"
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

   auto options = forge::yaml::read_options{};
   options.unknown_fields = forge::yaml::unknown_field_policy::error;
   const auto rejected = forge::yaml::read<forge_yaml_tests::http_config>("bind-port: 9090\n"
                                                                      "extra: 1\n",
                                                                      options);
   BOOST_TEST(!rejected.ok());

   const auto invalid = forge::yaml::read<forge_yaml_tests::http_config>("bind-port: 0\n");
   BOOST_TEST(!invalid.ok());
}

BOOST_AUTO_TEST_CASE(yaml_typed_load_uses_same_unknown_policy_as_read) {
   const auto path = std::filesystem::temp_directory_path() /
                     ("forge_yaml_unknown_policy_" +
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

   const auto warned = forge::yaml::load<forge_yaml_tests::http_config>(path);
   BOOST_REQUIRE(warned.ok());
   BOOST_REQUIRE_EQUAL(warned.diagnostics.size(), 1U);
   BOOST_TEST(warned.diagnostics.front().code == "yaml.unknown");

   auto rejected_options = forge::yaml::read_options{};
   rejected_options.unknown_fields = forge::yaml::unknown_field_policy::error;
   const auto rejected = forge::yaml::load<forge_yaml_tests::http_config>(path, rejected_options);
   BOOST_TEST(!rejected.ok());
   BOOST_REQUIRE_EQUAL(rejected.diagnostics.size(), 1U);
   BOOST_TEST(rejected.diagnostics.front().code == "yaml.unknown");

   auto ignored_options = forge::yaml::read_options{};
   ignored_options.unknown_fields = forge::yaml::unknown_field_policy::ignore;
   const auto ignored = forge::yaml::load<forge_yaml_tests::http_config>(path, ignored_options);
   BOOST_REQUIRE(ignored.ok());
   BOOST_TEST(ignored.diagnostics.empty());
   BOOST_TEST(ignored.value.bind_port == 9090U);
}

BOOST_AUTO_TEST_CASE(yaml_malformed_input_returns_forge_diagnostic) {
   const auto parsed = forge::yaml::read_value("root: [unterminated\n");
   BOOST_TEST(!parsed.ok());
   BOOST_REQUIRE_EQUAL(parsed.diagnostics.size(), 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "yaml.parse");
   BOOST_TEST(parsed.diagnostics.front().message.find("glz::") == std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
