#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace forge_json_tests {

struct http_config {
   std::uint16_t bind_port = 0;
   std::string bind_host;
   bool tls_enabled = false;
   std::vector<std::string> tags;
};

} // namespace forge_json_tests

BOOST_DESCRIBE_STRUCT(forge_json_tests::http_config, (), (bind_port, bind_host, tls_enabled, tags))

import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.json;
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

template <> struct forge::schema::rules<forge_json_tests::http_config> {
   [[nodiscard]] static forge::schema::object_schema<forge_json_tests::http_config> define() {
      auto schema = forge::schema::object<forge_json_tests::http_config>();
      schema.field<&forge_json_tests::http_config::bind_port>("bind-port").required().default_value(8080).range(1, 65535);
      schema.field<&forge_json_tests::http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&forge_json_tests::http_config::tls_enabled>("tls-enabled").default_value(false);
      static_cast<void>(schema.field<&forge_json_tests::http_config::tags>("tags"));
      return schema;
   }
};

BOOST_AUTO_TEST_SUITE(json_codec_tests)

BOOST_AUTO_TEST_CASE(json_value_roundtrip_preserves_generic_shapes) {
   const auto parsed = forge::json::read_value(R"({"null":null,"flag":true,"i":-2,"u":7,"d":3.5,"s":"x","a":[1,"b"]})");
   BOOST_REQUIRE(parsed.ok());

   const auto& object = parsed.value.get_object();
   BOOST_TEST(object["flag"].as_bool());
   BOOST_TEST(object["i"].as_int64() == -2);
   BOOST_TEST(object["u"].as_uint64() == 7U);
   BOOST_TEST(object["d"].as_double() == 3.5);
   BOOST_TEST(object["s"].get_string() == "x");
   BOOST_REQUIRE_EQUAL(object["a"].get_array().size(), 2U);

   const auto written = forge::json::write_value(parsed.value);
   BOOST_REQUIRE(written.ok());
   const auto reparsed = forge::json::read_value(written.text);
   BOOST_REQUIRE(reparsed.ok());
   BOOST_TEST(reparsed.value.get_object()["flag"].as_bool());
   BOOST_TEST(reparsed.value.get_object()["i"].as_int64() == -2);
   BOOST_TEST(reparsed.value.get_object()["u"].as_uint64() == 7U);
   BOOST_REQUIRE_EQUAL(reparsed.value.get_object()["a"].get_array().size(), 2U);
}

BOOST_AUTO_TEST_CASE(json_large_uint64_is_not_silently_converted_to_double) {
   const auto parsed = forge::json::read_value(R"({"max":18446744073709551615})");
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.get_object()["max"].as_uint64() == 18446744073709551615ULL);

   const auto written = forge::json::write_value(parsed.value);
   BOOST_REQUIRE(written.ok());
   BOOST_TEST(written.text.find("18446744073709551615") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(json_document_roundtrip_uses_config_document) {
   auto document = forge::config::document{};
   document.set("http.bind-host", "127.0.0.1");
   document.set("http.bind-port", 8080);
   document.set("http.tags", forge::config::value::array_type{forge::config::value{"alpha"}, forge::config::value{"beta"}});

   const auto written = forge::json::write_document(document, {.pretty = true});
   BOOST_REQUIRE(written.ok());
   const auto parsed = forge::json::read_document(written.text);
   BOOST_REQUIRE(parsed.ok());
   BOOST_REQUIRE(parsed.value.try_get("http.bind-host") != nullptr);
   BOOST_REQUIRE(parsed.value.try_get("http.bind-port") != nullptr);
   BOOST_REQUIRE(parsed.value.try_get("http.tags") != nullptr);
}

BOOST_AUTO_TEST_CASE(json_typed_read_uses_schema_defaults_validation_and_unknown_policy) {
   const auto parsed = forge::json::read<forge_json_tests::http_config>(
       R"({"bind-port":9090,"tls-enabled":false,"tags":["alpha"],"extra":1})");
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.bind_port == 9090U);
   BOOST_TEST(parsed.value.bind_host == "127.0.0.1");
   BOOST_REQUIRE_EQUAL(parsed.value.tags.size(), 1U);
   BOOST_TEST(parsed.diagnostics.size() == 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "json.unknown");

   auto options = forge::json::read_options{};
   options.unknown_fields = forge::json::unknown_field_policy::error;
   const auto rejected = forge::json::read<forge_json_tests::http_config>(R"({"bind-port":9090,"extra":1})", options);
   BOOST_TEST(!rejected.ok());
   BOOST_TEST(rejected.diagnostics.front().code == "json.unknown");

   const auto invalid = forge::json::read<forge_json_tests::http_config>(R"({"bind-port":0})");
   BOOST_TEST(!invalid.ok());
}

BOOST_AUTO_TEST_CASE(json_typed_load_uses_same_unknown_policy_as_read) {
   const auto path = std::filesystem::temp_directory_path() /
                     ("forge_json_unknown_policy_" +
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

   const auto warned = forge::json::load<forge_json_tests::http_config>(path);
   BOOST_REQUIRE(warned.ok());
   BOOST_REQUIRE_EQUAL(warned.diagnostics.size(), 1U);
   BOOST_TEST(warned.diagnostics.front().code == "json.unknown");

   auto rejected_options = forge::json::read_options{};
   rejected_options.unknown_fields = forge::json::unknown_field_policy::error;
   const auto rejected = forge::json::load<forge_json_tests::http_config>(path, rejected_options);
   BOOST_TEST(!rejected.ok());
   BOOST_REQUIRE_EQUAL(rejected.diagnostics.size(), 1U);
   BOOST_TEST(rejected.diagnostics.front().code == "json.unknown");

   auto ignored_options = forge::json::read_options{};
   ignored_options.unknown_fields = forge::json::unknown_field_policy::ignore;
   const auto ignored = forge::json::load<forge_json_tests::http_config>(path, ignored_options);
   BOOST_REQUIRE(ignored.ok());
   BOOST_TEST(ignored.diagnostics.empty());
   BOOST_TEST(ignored.value.bind_port == 9090U);
}

BOOST_AUTO_TEST_CASE(json_malformed_input_returns_forge_diagnostic) {
   const auto parsed = forge::json::read_value(R"({"unterminated":)");
   BOOST_TEST(!parsed.ok());
   BOOST_REQUIRE_EQUAL(parsed.diagnostics.size(), 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "json.parse");
   BOOST_TEST(parsed.diagnostics.front().message.find("glz::") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(json_write_escapes_control_bytes_inside_strings) {
   const auto expected = std::string{"a\x01\b\0z", 5};
   const auto written = forge::json::write_value(forge::variant{forge::mutable_variant_object{}("text", expected)});
   BOOST_REQUIRE(written.ok());
   BOOST_TEST(written.text.find("\\u0001") != std::string::npos);
   const auto escaped_backspace =
       written.text.find("\\b") != std::string::npos || written.text.find("\\u0008") != std::string::npos;
   BOOST_TEST(escaped_backspace);
   BOOST_TEST(written.text.find("\\u0000") != std::string::npos);
   BOOST_TEST(written.text.find('\0') == std::string::npos);

   const auto parsed = forge::json::read_value(written.text);
   BOOST_REQUIRE(parsed.ok());
   BOOST_TEST(parsed.value.get_object()["text"].get_string() == expected);
}

BOOST_AUTO_TEST_SUITE_END()
