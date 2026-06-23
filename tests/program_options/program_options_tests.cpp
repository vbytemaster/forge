#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.program_options;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;

namespace {

struct http_config {
   std::uint16_t bind_port = 0;
   bool tls_enabled = true;
   std::vector<std::string> tags;
};

struct flat_config {
   std::string log_level;
};

} // namespace

BOOST_DESCRIBE_STRUCT(http_config, (), (bind_port, tls_enabled, tags))
BOOST_DESCRIBE_STRUCT(flat_config, (), (log_level))

template <> struct forge::schema::rules<http_config> {
   [[nodiscard]] static forge::schema::object_schema<http_config> define() {
      auto schema = forge::schema::object<http_config>();
      schema.field<&http_config::bind_port>("bind-port").alias("port").range(1, 65535);
      schema.field<&http_config::tls_enabled>("tls-enabled").default_value(true);
      static_cast<void>(schema.field<&http_config::tags>("tags"));
      return schema;
   }
};

template <> struct forge::schema::rules<flat_config> {
   [[nodiscard]] static forge::schema::object_schema<flat_config> define() {
      auto schema = forge::schema::object<flat_config>();
      schema.field<&flat_config::log_level>("log-level").default_value("info");
      return schema;
   }
};

BOOST_AUTO_TEST_CASE(program_options_parses_cli_into_config_document) {
   auto registry = forge::config::component_registry{};
   registry.add(forge::config::describe_component<http_config>("http"));

   const char* argv[] = {
       "tool", "--http.bind-port=9090", "--http.tls-enabled=false", "--http.tags=alpha", "--http.tags=beta",
   };
   const auto parsed = forge::program_options::parse(5, argv, registry);
   BOOST_TEST(parsed.ok());

   const auto decoded = forge::config::decode<http_config>(parsed.document, "http");
   BOOST_TEST(decoded.ok());
   BOOST_TEST(decoded.value.bind_port == 9090U);
   BOOST_TEST(!decoded.value.tls_enabled);
   BOOST_REQUIRE_EQUAL(decoded.value.tags.size(), 2U);
   BOOST_TEST(decoded.value.tags.front() == "alpha");
}

BOOST_AUTO_TEST_CASE(program_options_reports_conversion_errors) {
   auto registry = forge::config::component_registry{};
   registry.add(forge::config::describe_component<http_config>("http"));

   const char* argv[] = {"tool", "--http.tls-enabled=maybe"};
   const auto parsed = forge::program_options::parse(2, argv, registry);
   BOOST_TEST(!parsed.ok());
   BOOST_REQUIRE_EQUAL(parsed.diagnostics.size(), 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "program_options.convert");
}

BOOST_AUTO_TEST_CASE(program_options_prescans_reserved_options_and_filters_application_args) {
   const auto reserved = std::vector<forge::program_options::reserved_option>{
      {
         .name = "runtime-threads",
         .path = "daemon.runtime-threads",
         .kind = forge::schema::value_kind::unsigned_integer,
      },
      {
         .name = "check-config",
         .path = "daemon.check-config",
         .kind = forge::schema::value_kind::boolean,
      },
      {
         .name = "profile",
         .path = "daemon.profile",
         .kind = forge::schema::value_kind::string,
      },
   };
   const char* argv[] = {
      "testd",
      "--runtime-threads=3",
      "--app.value=on",
      "--check-config",
      "--profile",
      "dev",
   };

   const auto parsed = forge::program_options::pre_scan_reserved(6, argv, reserved);
   BOOST_TEST(parsed.ok());
   BOOST_TEST(parsed.present("daemon.runtime-threads"));
   BOOST_TEST(parsed.present("daemon.check-config"));
   BOOST_TEST(parsed.present("daemon.profile"));

   const auto* runtime_threads = parsed.document.try_get("daemon.runtime-threads");
   BOOST_REQUIRE(runtime_threads != nullptr);
   BOOST_TEST(std::get<std::uint64_t>(runtime_threads->storage) == 3U);

   const auto* check_config = parsed.document.try_get("daemon.check-config");
   BOOST_REQUIRE(check_config != nullptr);
   BOOST_TEST(std::get<bool>(check_config->storage));

   const auto* profile = parsed.document.try_get("daemon.profile");
   BOOST_REQUIRE(profile != nullptr);
   BOOST_TEST(std::get<std::string>(profile->storage) == "dev");

   const auto expected = std::vector<std::string>{"testd", "--app.value=on"};
   BOOST_TEST(parsed.filtered_args == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(program_options_supports_empty_component_section_as_flat_flags) {
   auto registry = forge::config::component_registry{};
   registry.add(forge::config::describe_component<flat_config>(""));

   const char* argv[] = {"tool", "--log-level=debug"};
   const auto parsed = forge::program_options::parse(2, argv, registry);
   BOOST_TEST(parsed.ok());

   const auto decoded = forge::config::decode<flat_config>(parsed.document);
   BOOST_TEST(decoded.ok());
   BOOST_TEST(decoded.value.log_level == "debug");
}

BOOST_AUTO_TEST_CASE(program_options_supports_flat_component_section) {
   auto registry = forge::config::component_registry{};
   registry.add(forge::config::describe_component<http_config>(""));

   const char* argv[] = {"tool", "--bind-port=8081", "--tls-enabled=false", "--tags=daemon"};
   const auto parsed = forge::program_options::parse(4, argv, registry);
   BOOST_TEST(parsed.ok());

   const auto decoded = forge::config::decode<http_config>(parsed.document);
   BOOST_TEST(decoded.ok());
   BOOST_TEST(decoded.value.bind_port == 8081U);
   BOOST_TEST(!decoded.value.tls_enabled);
   BOOST_REQUIRE_EQUAL(decoded.value.tags.size(), 1U);
   BOOST_TEST(decoded.value.tags.front() == "daemon");
}
