#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace forge_schema_tests {

struct http_config {
   std::uint16_t bind_port = 0;
   std::string bind_host;
   bool tls_enabled = false;
   std::vector<std::string> tags;
   std::string token;
};

} // namespace forge_schema_tests

BOOST_DESCRIBE_STRUCT(forge_schema_tests::http_config, (), (bind_port, bind_host, tls_enabled, tags, token))

import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;
import forge.schema.scalar;

template <> struct forge::schema::rules<forge_schema_tests::http_config> {
   [[nodiscard]] static forge::schema::object_schema<forge_schema_tests::http_config> define() {
      auto schema = forge::schema::object<forge_schema_tests::http_config>();
      schema.field<&forge_schema_tests::http_config::bind_port>("bind-port")
          .required()
          .default_value(8080)
          .range(1, 65535)
          .description("HTTP bind port");
      schema.field<&forge_schema_tests::http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&forge_schema_tests::http_config::tls_enabled>("tls-enabled").default_value(false);
      static_cast<void>(schema.field<&forge_schema_tests::http_config::tags>("tags"));
      schema.field<&forge_schema_tests::http_config::token>("token").secret().deprecated("use vault-ref");
      return schema;
   }
};

BOOST_AUTO_TEST_CASE(schema_describes_fields_defaults_and_validation) {
   const auto schema = forge::schema::rules<forge_schema_tests::http_config>::define();
   BOOST_REQUIRE_EQUAL(schema.fields().size(), 5U);
   BOOST_TEST(schema.fields()[0].name == "bind-port");
   BOOST_TEST(schema.fields()[0].required);
   BOOST_TEST(schema.fields()[4].secret);
   BOOST_TEST(schema.fields()[4].deprecated);

   auto config = forge_schema_tests::http_config{};
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
   auto level = forge::schema::severity::info;
   BOOST_TEST(forge::schema::enum_from_string("warning", level));
   BOOST_TEST(static_cast<int>(level) == static_cast<int>(forge::schema::severity::warning));
   BOOST_TEST(forge::schema::enum_to_string(forge::schema::severity::error).value() == "error");
   BOOST_TEST(forge::schema::enum_from_int(0, level));
   BOOST_TEST(static_cast<int>(level) == static_cast<int>(forge::schema::severity::info));
}

BOOST_AUTO_TEST_CASE(schema_checked_integral_cast_handles_widening_and_narrowing) {
   BOOST_TEST(forge::schema::checked_integral_cast<long long>(int{-1}) == -1LL);
   BOOST_TEST(forge::schema::checked_integral_cast<std::int64_t>(std::int32_t{-123}) == -123);
   BOOST_TEST(forge::schema::checked_integral_cast<long long>(std::uint32_t{123}) == 123LL);

   BOOST_CHECK_THROW(static_cast<void>(forge::schema::checked_integral_cast<std::uint8_t>(std::uint16_t{256})),
                     std::invalid_argument);
   BOOST_CHECK_THROW(static_cast<void>(forge::schema::checked_integral_cast<std::int8_t>(std::int16_t{128})),
                     std::invalid_argument);
   BOOST_CHECK_THROW(static_cast<void>(forge::schema::checked_integral_cast<std::uint8_t>(std::int16_t{-1})),
                     std::invalid_argument);
}
