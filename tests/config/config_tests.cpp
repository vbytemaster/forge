#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

import fcl.config;
import fcl.schema;

namespace {

struct http_config {
   std::uint16_t bind_port = 0;
   std::string bind_host;
   bool tls_enabled = false;
   std::vector<std::string> tags;
   std::string token;
};

} // namespace

BOOST_DESCRIBE_STRUCT(http_config, (), (bind_port, bind_host, tls_enabled, tags, token))

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
