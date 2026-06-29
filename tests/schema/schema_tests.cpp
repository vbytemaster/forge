#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <optional>
#include <ranges>
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

struct optional_config {
   std::optional<std::string> token;
   std::optional<std::uint16_t> port;
};

struct optional_default_config {
   std::optional<std::uint16_t> wrapped_port;
   std::optional<std::uint16_t> raw_port;
};

struct optional_list_item {
   std::string id;
};

struct optional_list_config {
   std::optional<std::vector<std::string>> tags;
   std::optional<std::vector<optional_list_item>> items;
};

} // namespace forge_schema_tests

BOOST_DESCRIBE_STRUCT(forge_schema_tests::http_config, (), (bind_port, bind_host, tls_enabled, tags, token))
BOOST_DESCRIBE_STRUCT(forge_schema_tests::optional_config, (), (token, port))
BOOST_DESCRIBE_STRUCT(forge_schema_tests::optional_default_config, (), (wrapped_port, raw_port))
BOOST_DESCRIBE_STRUCT(forge_schema_tests::optional_list_item, (), (id))
BOOST_DESCRIBE_STRUCT(forge_schema_tests::optional_list_config, (), (tags, items))

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

template <> struct forge::schema::rules<forge_schema_tests::optional_config> {
   [[nodiscard]] static forge::schema::object_schema<forge_schema_tests::optional_config> define() {
      auto schema = forge::schema::object<forge_schema_tests::optional_config>();
      schema.field<&forge_schema_tests::optional_config::token>("token").non_empty();
      schema.field<&forge_schema_tests::optional_config::port>("port").range(1, 65535);
      return schema;
   }
};

template <> struct forge::schema::rules<forge_schema_tests::optional_default_config> {
   [[nodiscard]] static forge::schema::object_schema<forge_schema_tests::optional_default_config> define() {
      auto schema = forge::schema::object<forge_schema_tests::optional_default_config>();
      schema.field<&forge_schema_tests::optional_default_config::wrapped_port>("wrapped-port")
          .default_value(std::optional<std::uint16_t>{443})
          .range(1, 65535);
      schema.field<&forge_schema_tests::optional_default_config::raw_port>("raw-port")
          .default_value(8443)
          .range(1, 65535);
      return schema;
   }
};

template <> struct forge::schema::rules<forge_schema_tests::optional_list_config> {
   [[nodiscard]] static forge::schema::object_schema<forge_schema_tests::optional_list_config> define() {
      auto schema = forge::schema::object<forge_schema_tests::optional_list_config>();
      schema.field<&forge_schema_tests::optional_list_config::tags>("tags").min_items(1).each_non_empty();
      schema.field<&forge_schema_tests::optional_list_config::items>("items")
          .items<forge_schema_tests::optional_list_item>()
          .unique_by<&forge_schema_tests::optional_list_item::id>();
      return schema;
   }
};

namespace {

[[nodiscard]] bool has_error_code(const std::vector<forge::schema::diagnostic>& diagnostics,
                                  std::string_view code) {
   return std::ranges::any_of(diagnostics, [&](const forge::schema::diagnostic& entry) {
      return entry.level == forge::schema::severity::error && entry.code == code;
   });
}

} // namespace

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

BOOST_AUTO_TEST_CASE(schema_optional_scalar_validators_unwrap_present_values_and_skip_absent) {
   const auto schema = forge::schema::rules<forge_schema_tests::optional_config>::define();

   auto absent = forge_schema_tests::optional_config{};
   BOOST_TEST(schema.validate(absent, "config").empty());

   auto valid = forge_schema_tests::optional_config{.token = "secret", .port = 443};
   BOOST_TEST(schema.validate(valid, "config").empty());

   auto invalid = forge_schema_tests::optional_config{.token = "", .port = 0};
   const auto diagnostics = schema.validate(invalid, "config");
   BOOST_REQUIRE_EQUAL(diagnostics.size(), 2U);
   BOOST_TEST(diagnostics[0].path == "config.token");
   BOOST_TEST(diagnostics[0].code == "schema.non_empty");
   BOOST_TEST(diagnostics[1].path == "config.port");
   BOOST_TEST(diagnostics[1].code == "schema.range");
}

BOOST_AUTO_TEST_CASE(schema_optional_defaults_apply_wrapped_and_raw_values) {
   const auto schema = forge::schema::rules<forge_schema_tests::optional_default_config>::define();

   auto config = forge_schema_tests::optional_default_config{};
   schema.apply_defaults(config);

   BOOST_REQUIRE(config.wrapped_port.has_value());
   BOOST_TEST(*config.wrapped_port == 443U);
   BOOST_REQUIRE(config.raw_port.has_value());
   BOOST_TEST(*config.raw_port == 8443U);
   BOOST_TEST(schema.validate(config, "config").empty());
}

BOOST_AUTO_TEST_CASE(schema_optional_list_validators_unwrap_present_values_and_skip_absent) {
   const auto schema = forge::schema::rules<forge_schema_tests::optional_list_config>::define();

   auto absent = forge_schema_tests::optional_list_config{};
   BOOST_TEST(schema.validate(absent, "config").empty());

   auto valid = forge_schema_tests::optional_list_config{
      .tags = std::vector<std::string>{"alpha"},
      .items = std::vector<forge_schema_tests::optional_list_item>{{.id = "a"}, {.id = "b"}},
   };
   BOOST_TEST(schema.validate(valid, "config").empty());

   auto empty_tags = forge_schema_tests::optional_list_config{.tags = std::vector<std::string>{}};
   const auto empty_tag_diagnostics = schema.validate(empty_tags, "config");
   BOOST_TEST(has_error_code(empty_tag_diagnostics, "schema.min_items"));

   auto invalid_tags = forge_schema_tests::optional_list_config{.tags = std::vector<std::string>{""}};
   const auto invalid_tag_diagnostics = schema.validate(invalid_tags, "config");
   BOOST_TEST(has_error_code(invalid_tag_diagnostics, "schema.non_empty"));

   auto duplicate_items = forge_schema_tests::optional_list_config{
      .items = std::vector<forge_schema_tests::optional_list_item>{{.id = "same"}, {.id = "same"}},
   };
   const auto duplicate_item_diagnostics = schema.validate(duplicate_items, "config");
   BOOST_TEST(has_error_code(duplicate_item_diagnostics, "schema.unique"));
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
