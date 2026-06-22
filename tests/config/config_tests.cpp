#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <any>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

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

namespace {

struct http_config {
   std::uint16_t bind_port = 0;
   std::string bind_host;
   bool tls_enabled = false;
   std::vector<std::string> tags;
   std::string token;
};

struct flat_config {
   std::string log_level;
};

struct nested_key_config {
   std::string id;
   std::string private_key;
   std::string input_profile = "fcl";
   std::vector<std::string> purposes;
};

struct nested_signer_config {
   std::vector<nested_key_config> keys;
   std::string default_output_profile = "fcl";
};

} // namespace

BOOST_DESCRIBE_STRUCT(http_config, (), (bind_port, bind_host, tls_enabled, tags, token))
BOOST_DESCRIBE_STRUCT(flat_config, (), (log_level))
BOOST_DESCRIBE_STRUCT(nested_key_config, (), (id, private_key, input_profile, purposes))
BOOST_DESCRIBE_STRUCT(nested_signer_config, (), (keys, default_output_profile))

template <> struct fcl::schema::rules<http_config> {
   [[nodiscard]] static fcl::schema::object_schema<http_config> define() {
      auto schema = fcl::schema::object<http_config>();
      schema.field<&http_config::bind_port>("bind-port").alias("port").required().default_value(8080).range(1, 65535);
      schema.field<&http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&http_config::tls_enabled>("tls-enabled").default_value(false);
      static_cast<void>(schema.field<&http_config::tags>("tags"));
      schema.field<&http_config::token>("token").secret().deprecated("use vault-ref");
      return schema;
   }
};

template <> struct fcl::schema::rules<flat_config> {
   [[nodiscard]] static fcl::schema::object_schema<flat_config> define() {
      auto schema = fcl::schema::object<flat_config>();
      schema.field<&flat_config::log_level>("log-level").default_value("info");
      return schema;
   }
};

template <> struct fcl::schema::rules<nested_key_config> {
   [[nodiscard]] static fcl::schema::object_schema<nested_key_config> define() {
      auto schema = fcl::schema::object<nested_key_config>();
      schema.field<&nested_key_config::id>("id").required().non_empty();
      schema.field<&nested_key_config::private_key>("private-key").required().non_empty().secret();
      schema.field<&nested_key_config::input_profile>("input-profile").default_value("fcl");
      schema.field<&nested_key_config::purposes>("purposes").min_items(1).each_non_empty();
      return schema;
   }
};

template <> struct fcl::schema::rules<nested_signer_config> {
   [[nodiscard]] static fcl::schema::object_schema<nested_signer_config> define() {
      auto schema = fcl::schema::object<nested_signer_config>();
      schema.field<&nested_signer_config::keys>("keys")
         .items<nested_key_config>()
         .secret()
         .unique_by<&nested_key_config::id>()
         .description("Configured local signing keys");
      schema.field<&nested_signer_config::default_output_profile>("default-output-profile").default_value("fcl");
      return schema;
   }
};

[[nodiscard]] bool has_diagnostic(const std::vector<fcl::schema::diagnostic>& entries,
                                  std::string_view path,
                                  std::string_view code) {
   return std::ranges::any_of(entries, [&](const fcl::schema::diagnostic& entry) {
      return entry.path == path && entry.code == code;
   });
}

BOOST_AUTO_TEST_CASE(config_key_path_splits_dotted_keys) {
   auto segments = fcl::config::key_path{.value = "http.bind-port"}.segments();
   BOOST_REQUIRE_EQUAL(segments.size(), 2U);
   BOOST_TEST(segments[0] == "http");
   BOOST_TEST(segments[1] == "bind-port");

   auto compacted = fcl::config::key_path{.value = ".http..tls-enabled."}.segments();
   BOOST_REQUIRE_EQUAL(compacted.size(), 2U);
   BOOST_TEST(compacted[0] == "http");
   BOOST_TEST(compacted[1] == "tls-enabled");
}

BOOST_AUTO_TEST_CASE(config_document_paths_merge_and_decode) {
   auto defaults = fcl::config::defaults_for<http_config>("http");
   auto file = fcl::config::document{};
   file.set("http.bind-port", 8081);
   auto cli = fcl::config::document{};
   cli.set("http.bind-port", 9090);
   cli.set("http.tls-enabled", false);
   cli.set("http.tags", fcl::config::value::array_type{fcl::config::value{"alpha"}, fcl::config::value{"beta"}});

   const auto merged = fcl::config::merge({defaults, file, cli});
   const auto decoded = fcl::config::decode<http_config>(merged, "http");
   BOOST_TEST(decoded.ok());
   BOOST_TEST(decoded.value.bind_port == 9090U);
   BOOST_TEST(decoded.value.bind_host == "127.0.0.1");
   BOOST_TEST(!decoded.value.tls_enabled);
   BOOST_REQUIRE_EQUAL(decoded.value.tags.size(), 2U);
   BOOST_TEST(decoded.value.tags[1] == "beta");
}

BOOST_AUTO_TEST_CASE(config_decode_rejects_integer_overflow_before_range_validation) {
   auto numeric = fcl::config::document{};
   numeric.set("http.bind-port", 70000);
   const auto decoded_numeric = fcl::config::decode<http_config>(numeric, "http");

   BOOST_TEST(!decoded_numeric.ok());
   BOOST_TEST(has_diagnostic(decoded_numeric.diagnostics.entries, "http.bind-port", "config.type"));
   BOOST_TEST(decoded_numeric.value.bind_port == 8080U);

   auto text = fcl::config::document{};
   text.set("http.bind-port", std::string{"70000"});
   const auto decoded_text = fcl::config::decode<http_config>(text, "http");

   BOOST_TEST(!decoded_text.ok());
   BOOST_TEST(has_diagnostic(decoded_text.diagnostics.entries, "http.bind-port", "config.type"));
   BOOST_TEST(decoded_text.value.bind_port == 8080U);

   auto trailing = fcl::config::document{};
   trailing.set("http.bind-port", std::string{"123abc"});
   const auto decoded_trailing = fcl::config::decode<http_config>(trailing, "http");

   BOOST_TEST(!decoded_trailing.ok());
   BOOST_TEST(has_diagnostic(decoded_trailing.diagnostics.entries, "http.bind-port", "config.type"));
   BOOST_TEST(decoded_trailing.value.bind_port == 8080U);
}

BOOST_AUTO_TEST_CASE(config_value_to_any_rejects_integer_overflow_and_trailing_junk) {
   BOOST_CHECK_THROW(
      static_cast<void>(fcl::config::value_to_any(
         fcl::config::value{std::numeric_limits<std::uint64_t>::max()},
         fcl::schema::value_kind::signed_integer)),
      std::invalid_argument);

   BOOST_CHECK_THROW(
      static_cast<void>(fcl::config::value_to_any(fcl::config::value{std::string{"123abc"}},
                                                 fcl::schema::value_kind::signed_integer)),
      std::invalid_argument);

   BOOST_CHECK_THROW(
      static_cast<void>(fcl::config::value_to_any(fcl::config::value{std::int64_t{-1}},
                                                 fcl::schema::value_kind::unsigned_integer)),
      std::invalid_argument);

   BOOST_CHECK_THROW(
      static_cast<void>(fcl::config::value_to_any(fcl::config::value{std::string{"123abc"}},
                                                 fcl::schema::value_kind::unsigned_integer)),
      std::invalid_argument);

   const auto valid = fcl::config::value_to_any(fcl::config::value{std::string{"123"}},
                                                fcl::schema::value_kind::unsigned_integer);
   BOOST_REQUIRE(valid.has_value());
   BOOST_TEST(std::any_cast<std::uint64_t>(valid) == 123U);
}

BOOST_AUTO_TEST_CASE(config_document_erase_and_rename_nested_keys) {
   auto doc = fcl::config::document{};
   doc.set("http.bind-port", 8080);
   doc.set("http.host", "127.0.0.1");
   doc.set("legacy.timeout", 30);

   BOOST_TEST(doc.rename("http.host", "http.bind-host"));
   BOOST_TEST(doc.try_get("http.host") == nullptr);
   const auto* host = doc.try_get("http.bind-host");
   BOOST_REQUIRE(host != nullptr);
   BOOST_TEST(std::get<std::string>(host->storage) == "127.0.0.1");

   BOOST_CHECK_THROW(static_cast<void>(doc.rename("http.bind-port", "http.bind-host")), std::invalid_argument);
   BOOST_TEST(doc.rename("http.bind-port", "http.bind-host", true));
   const auto* overwritten = doc.try_get("http.bind-host");
   BOOST_REQUIRE(overwritten != nullptr);
   BOOST_TEST(std::get<std::int64_t>(overwritten->storage) == 8080);

   BOOST_TEST(doc.erase("legacy.timeout"));
   BOOST_TEST(doc.try_get("legacy.timeout") == nullptr);
   BOOST_TEST(!doc.erase("legacy.timeout"));
   BOOST_TEST(!doc.rename("missing.value", "http.missing"));
}

BOOST_AUTO_TEST_CASE(config_reports_required_unknown_deprecated_and_redacts) {
   auto doc = fcl::config::document{};
   doc.set("http.bind-host", "127.0.0.1");
   doc.set("http.token", "secret-value");
   doc.set("http.extra", "ignored");

   const auto decoded = fcl::config::decode<http_config>(doc, "http");
   BOOST_TEST(!decoded.ok());
   BOOST_TEST(decoded.diagnostics.entries.size() >= 3U);

   auto registry = fcl::config::component_registry{};
   registry.add(fcl::config::describe_component<http_config>("http"));
   auto redacted = fcl::config::redact(doc, registry);
   const auto* token = redacted.try_get("http.token");
   BOOST_REQUIRE(token != nullptr);
   BOOST_TEST(std::get<std::string>(token->storage) == "<redacted>");
}

BOOST_AUTO_TEST_CASE(config_registry_rejects_duplicate_aliases) {
   auto registry = fcl::config::component_registry{};
   registry.add(fcl::config::describe_component<http_config>("http"));
   BOOST_CHECK_THROW(registry.add(fcl::config::describe_component<http_config>("http")), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(config_registry_supports_empty_component_sections) {
   auto registry = fcl::config::component_registry{};
   registry.add(fcl::config::describe_component<flat_config>(""));

   auto doc = fcl::config::document{};
   doc.set("log-level", "debug");

   const auto view = fcl::config::component_view{doc, ""};
   BOOST_TEST(view.get_or<std::string>("log-level", "info") == "debug");

   const auto decoded = fcl::config::decode<flat_config>(doc);
   BOOST_TEST(decoded.ok());
   BOOST_TEST(decoded.value.log_level == "debug");
   BOOST_REQUIRE_EQUAL(registry.components().front().fields.size(), 1U);
   BOOST_TEST(registry.components().front().fields.front().has_default);
}

BOOST_AUTO_TEST_CASE(config_component_view_rejects_integer_overflow) {
   auto doc = fcl::config::document{};
   doc.set("http.small", std::uint64_t{70000});
   doc.set("http.negative", std::int64_t{-1});

   const auto view = fcl::config::component_view{doc, "http"};
   BOOST_CHECK_THROW(static_cast<void>(view.get_or<std::uint16_t>("small", 0)), std::invalid_argument);
   BOOST_CHECK_THROW(static_cast<void>(view.get_or<std::uint16_t>("negative", 0)), std::invalid_argument);
   BOOST_TEST(view.get_or<std::uint16_t>("missing", 42) == 42U);
}

BOOST_AUTO_TEST_CASE(config_decodes_nested_object_lists_with_item_defaults_and_paths) {
   auto key = fcl::config::value::object_type{};
   key["id"] = fcl::config::value{"provider"};
   key["private-key"] = fcl::config::value{"PVT_FAKE"};
   key["purposes"] = fcl::config::value::array_type{fcl::config::value{"storage.receipt"}};
   key["unknown"] = fcl::config::value{"ignored"};

   auto doc = fcl::config::document{};
   doc.set("signature-provider.keys", fcl::config::value::array_type{fcl::config::value{key}});

   const auto decoded = fcl::config::decode<nested_signer_config>(doc, "signature-provider");
   BOOST_TEST(decoded.ok());
   BOOST_REQUIRE_EQUAL(decoded.value.keys.size(), 1U);
   BOOST_TEST(decoded.value.keys.front().id == "provider");
   BOOST_TEST(decoded.value.keys.front().private_key == "PVT_FAKE");
   BOOST_TEST(decoded.value.keys.front().input_profile == "fcl");
   BOOST_TEST(decoded.value.default_output_profile == "fcl");
   BOOST_TEST(has_diagnostic(decoded.diagnostics.entries, "signature-provider.keys[0].unknown", "config.unknown"));
}

BOOST_AUTO_TEST_CASE(config_nested_object_list_validators_report_stable_diagnostics) {
   auto invalid = fcl::config::value::object_type{};
   invalid["id"] = fcl::config::value{""};
   invalid["private-key"] = fcl::config::value{""};
   invalid["purposes"] = fcl::config::value::array_type{fcl::config::value{""}};

   auto duplicate = fcl::config::value::object_type{};
   duplicate["id"] = fcl::config::value{"duplicate"};
   duplicate["private-key"] = fcl::config::value{"PVT_ONE"};
   duplicate["purposes"] = fcl::config::value::array_type{fcl::config::value{"storage.receipt"}};

   auto duplicate_two = duplicate;
   duplicate_two["private-key"] = fcl::config::value{"PVT_TWO"};

   auto doc = fcl::config::document{};
   doc.set("signature-provider.keys",
           fcl::config::value::array_type{
              fcl::config::value{invalid},
              fcl::config::value{duplicate},
              fcl::config::value{duplicate_two},
           });

   const auto decoded = fcl::config::decode<nested_signer_config>(doc, "signature-provider");
   BOOST_TEST(!decoded.ok());
   BOOST_TEST(has_diagnostic(decoded.diagnostics.entries, "signature-provider.keys[0].id", "schema.non_empty"));
   BOOST_TEST(has_diagnostic(decoded.diagnostics.entries, "signature-provider.keys[0].private-key", "schema.non_empty"));
   BOOST_TEST(has_diagnostic(decoded.diagnostics.entries, "signature-provider.keys[0].purposes[0]", "schema.non_empty"));
   BOOST_TEST(has_diagnostic(decoded.diagnostics.entries, "signature-provider.keys", "schema.unique"));
}

BOOST_AUTO_TEST_CASE(config_formats_full_decode_diagnostics) {
   auto invalid = fcl::config::value::object_type{};
   invalid["id"] = fcl::config::value{""};
   invalid["private-key"] = fcl::config::value{""};
   invalid["purposes"] = fcl::config::value::array_type{fcl::config::value{""}};

   auto doc = fcl::config::document{};
   doc.set("signature-provider.keys", fcl::config::value::array_type{fcl::config::value{invalid}});

   const auto decoded = fcl::config::decode<nested_signer_config>(doc, "signature-provider");
   BOOST_TEST(!decoded.ok());

   const auto message = fcl::config::format_decode_diagnostics("invalid signature provider config",
                                                               decoded.diagnostics);
   BOOST_TEST(message.find("invalid signature provider config") != std::string::npos);
   BOOST_TEST(message.find("signature-provider.keys[0].id schema.non_empty") != std::string::npos);
   BOOST_TEST(message.find("signature-provider.keys[0].private-key schema.non_empty") != std::string::npos);
   BOOST_TEST(message.find("signature-provider.keys[0].purposes[0] schema.non_empty") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(config_describes_secret_object_list_without_nested_env_fields) {
   const auto descriptor = fcl::config::describe_component<nested_signer_config>("signature-provider");
   BOOST_REQUIRE_EQUAL(descriptor.fields.size(), 2U);
   BOOST_TEST(descriptor.fields[0].name == "keys");
   BOOST_TEST(static_cast<int>(descriptor.fields[0].kind) == static_cast<int>(fcl::schema::value_kind::object_list));
   BOOST_TEST(descriptor.fields[0].secret);
   BOOST_TEST(descriptor.fields[1].name == "default-output-profile");
}

BOOST_AUTO_TEST_CASE(config_migration_chain_updates_document_version) {
   auto doc = fcl::config::document{};
   doc.set("http.port", 8080);

   auto plan = fcl::config::migration_plan{};
   plan.step(0, 1, "rename port", [](fcl::config::document& input) {
      static_cast<void>(input.rename("http.port", "http.bind-port"));
   });
   plan.step(1, 2, "add host", [](fcl::config::document& input) {
      input.set("http.bind-host", "127.0.0.1");
   });

   const auto migrated = fcl::config::migrate(std::move(doc), plan);
   BOOST_TEST(migrated.ok());
   BOOST_TEST(migrated.from_version == 0U);
   BOOST_TEST(migrated.to_version == 2U);
   BOOST_TEST(migrated.value.try_get("http.port") == nullptr);
   BOOST_REQUIRE(migrated.value.try_get("http.bind-port") != nullptr);
   BOOST_REQUIRE(migrated.value.try_get("http.bind-host") != nullptr);
   const auto* version = migrated.value.try_get("version");
   BOOST_REQUIRE(version != nullptr);
   BOOST_TEST(std::get<std::uint64_t>(version->storage) == 2U);
}

BOOST_AUTO_TEST_CASE(config_migration_reports_missing_and_future_versions) {
   auto plan = fcl::config::migration_plan{};
   plan.step(0, 1, "first", [](fcl::config::document&) {});
   plan.step(2, 3, "gap", [](fcl::config::document&) {});

   auto missing = fcl::config::migrate(fcl::config::document{}, plan);
   BOOST_TEST(!missing.ok());
   BOOST_REQUIRE_EQUAL(missing.diagnostics.size(), 1U);
   BOOST_TEST(missing.diagnostics.front().code == "config.migration.missing-step");

   auto future_doc = fcl::config::document{};
   future_doc.set("version", 9U);
   auto future = fcl::config::migrate(std::move(future_doc), plan);
   BOOST_TEST(!future.ok());
   BOOST_REQUIRE_EQUAL(future.diagnostics.size(), 1U);
   BOOST_TEST(future.diagnostics.front().code == "config.migration.future-version");
}
