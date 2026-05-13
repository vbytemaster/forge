#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>
#include <vector>

import fcl.config;
import fcl.program_options;
import fcl.schema;

namespace {

struct http_config {
   std::uint16_t bind_port = 0;
   bool tls_enabled = true;
   std::vector<std::string> tags;
};

} // namespace

BOOST_DESCRIBE_STRUCT(http_config, (), (bind_port, tls_enabled, tags))

template <> struct fcl::schema::rules<http_config> {
   [[nodiscard]] static fcl::schema::object_schema<http_config> define() {
      auto schema = fcl::schema::object<http_config>();
      schema.field<&http_config::bind_port>("bind-port").alias("port").range(1, 65535);
      schema.field<&http_config::tls_enabled>("tls-enabled").default_value(true);
      static_cast<void>(schema.field<&http_config::tags>("tags"));
      return schema;
   }
};

BOOST_AUTO_TEST_CASE(program_options_parses_cli_into_config_document) {
   auto registry = fcl::config::component_registry{};
   registry.add(fcl::config::describe_component<http_config>("http"));

   const char* argv[] = {
       "tool", "--http.bind-port=9090", "--http.tls-enabled=false", "--http.tags=alpha", "--http.tags=beta",
   };
   const auto parsed = fcl::program_options::parse(5, argv, registry);
   BOOST_TEST(parsed.ok());

   const auto decoded = fcl::config::decode<http_config>(parsed.document, "http");
   BOOST_TEST(decoded.ok());
   BOOST_TEST(decoded.value.bind_port == 9090U);
   BOOST_TEST(!decoded.value.tls_enabled);
   BOOST_REQUIRE_EQUAL(decoded.value.tags.size(), 2U);
   BOOST_TEST(decoded.value.tags.front() == "alpha");
}

BOOST_AUTO_TEST_CASE(program_options_reports_conversion_errors) {
   auto registry = fcl::config::component_registry{};
   registry.add(fcl::config::describe_component<http_config>("http"));

   const char* argv[] = {"tool", "--http.tls-enabled=maybe"};
   const auto parsed = fcl::program_options::parse(2, argv, registry);
   BOOST_TEST(!parsed.ok());
   BOOST_REQUIRE_EQUAL(parsed.diagnostics.size(), 1U);
   BOOST_TEST(parsed.diagnostics.front().code == "program_options.convert");
}

BOOST_AUTO_TEST_CASE(program_options_supports_flat_component_section) {
   auto registry = fcl::config::component_registry{};
   registry.add(fcl::config::describe_component<http_config>(""));

   const char* argv[] = {"tool", "--bind-port=8081", "--tls-enabled=false", "--tags=daemon"};
   const auto parsed = fcl::program_options::parse(4, argv, registry);
   BOOST_TEST(parsed.ok());

   const auto decoded = fcl::config::decode<http_config>(parsed.document);
   BOOST_TEST(decoded.ok());
   BOOST_TEST(decoded.value.bind_port == 8081U);
   BOOST_TEST(!decoded.value.tls_enabled);
   BOOST_REQUIRE_EQUAL(decoded.value.tags.size(), 1U);
   BOOST_TEST(decoded.value.tags.front() == "daemon");
}
