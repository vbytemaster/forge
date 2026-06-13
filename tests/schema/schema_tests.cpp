#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace fcl_schema_tests {

struct http_config {
   std::uint16_t bind_port = 0;
   std::string bind_host;
   bool tls_enabled = false;
   std::vector<std::string> tags;
   std::string token;
};

} // namespace fcl_schema_tests

BOOST_DESCRIBE_STRUCT(fcl_schema_tests::http_config, (), (bind_port, bind_host, tls_enabled, tags, token))

import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

template <> struct fcl::schema::rules<fcl_schema_tests::http_config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl_schema_tests::http_config> define() {
      auto schema = fcl::schema::object<fcl_schema_tests::http_config>();
      schema.field<&fcl_schema_tests::http_config::bind_port>("bind-port")
          .required()
          .default_value(8080)
          .range(1, 65535)
          .description("HTTP bind port");
      schema.field<&fcl_schema_tests::http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&fcl_schema_tests::http_config::tls_enabled>("tls-enabled").default_value(false);
      static_cast<void>(schema.field<&fcl_schema_tests::http_config::tags>("tags"));
      schema.field<&fcl_schema_tests::http_config::token>("token").secret().deprecated("use vault-ref");
      return schema;
   }
};

BOOST_AUTO_TEST_CASE(schema_describes_fields_defaults_and_validation) {
   const auto schema = fcl::schema::rules<fcl_schema_tests::http_config>::define();
   BOOST_REQUIRE_EQUAL(schema.fields().size(), 5U);
   BOOST_TEST(schema.fields()[0].name == "bind-port");
   BOOST_TEST(schema.fields()[0].required);
   BOOST_TEST(schema.fields()[4].secret);
   BOOST_TEST(schema.fields()[4].deprecated);

   auto config = fcl_schema_tests::http_config{};
   schema.apply_defaults(config);
   BOOST_TEST(config.bind_port == 8080U);
   BOOST_TEST(config.bind_host == "127.0.0.1");
   BOOST_TEST(!config.tls_enabled);

   config.bind_port = 0;
   const auto diagnostics = schema.validate(config, "http");
   BOOST_REQUIRE_EQUAL(diagnostics.size(), 1U);
   BOOST_TEST(diagnostics.front().path == "http.bind-port");
   BOOST_TEST(diagnostics.front().code == "schema.range");
}

BOOST_AUTO_TEST_CASE(schema_converts_described_enums) {
   auto level = fcl::schema::severity::info;
   BOOST_TEST(fcl::schema::enum_from_string("warning", level));
   BOOST_TEST(static_cast<int>(level) == static_cast<int>(fcl::schema::severity::warning));
   BOOST_TEST(fcl::schema::enum_to_string(fcl::schema::severity::error).value() == "error");
   BOOST_TEST(fcl::schema::enum_from_int(0, level));
   BOOST_TEST(static_cast<int>(level) == static_cast<int>(fcl::schema::severity::info));
}
