#include <boost/asio/awaitable.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/log/macros.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

import forge.api.registry;
import forge.app.diagnostics;
import forge.app.events;
import forge.app.plugin_context;
import forge.app.signals;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.asio.task_scheduler;
import forge.config.component;
import forge.config.document;
import forge.config.value;
import forge.http.route_context;
import forge.http.server;
import forge.http.types;
import forge.log.logger;
import forge.log.logger_config;
import forge.plugins.log.otlp.api;
import forge.plugins.log.otlp.exceptions;
import forge.plugins.log.otlp.plugin;
import forge.plugins.log.otlp.types;
import forge.variant.value;

namespace {

using namespace std::chrono_literals;
namespace log_otlp = forge::plugins::log::otlp;

struct collected_request {
   std::string target;
   std::string content_type;
   std::string body;
};

class fake_collector {
 public:
   explicit fake_collector(forge::asio::runtime& runtime)
       : server_(runtime, {.bind_address = "127.0.0.1", .port = 0},
                 [this](forge::http::route_context& context) { return handle(context); }) {
      server_.start();
      for (auto attempt = 0; attempt != 100; ++attempt) {
         if (server_.port() != 0) {
            return;
         }
         std::this_thread::sleep_for(10ms);
      }
      BOOST_FAIL("fake OTLP logs collector did not bind a port");
   }

   ~fake_collector() {
      server_.stop();
      std::this_thread::sleep_for(20ms);
   }

   [[nodiscard]] std::string endpoint() const {
      return "http://127.0.0.1:" + std::to_string(server_.port());
   }

   [[nodiscard]] std::vector<collected_request> requests() const {
      const auto lock = std::scoped_lock{mutex_};
      return requests_;
   }

   [[nodiscard]] bool wait_for_requests(std::size_t count, std::chrono::milliseconds timeout = 2s) const {
      auto lock = std::unique_lock{mutex_};
      return ready_.wait_for(lock, timeout, [&] { return requests_.size() >= count; });
   }

 private:
   boost::asio::awaitable<forge::http::response> handle(forge::http::route_context& context) {
      auto request = collected_request{
         .target = std::string{context.request.target()},
         .body = context.request.body(),
      };
      if (const auto header = context.request.find(forge::http::field::content_type);
          header != context.request.end()) {
         request.content_type = std::string{header->value()};
      }
      {
         const auto lock = std::scoped_lock{mutex_};
         requests_.push_back(std::move(request));
      }
      ready_.notify_all();

      auto response = forge::http::response{forge::http::status::ok, context.request.version()};
      response.set(forge::http::field::content_type, "application/json");
      response.body() = "{}";
      response.prepare_payload();
      co_return response;
   }

   forge::http::server server_;
   mutable std::mutex mutex_;
   mutable std::condition_variable ready_;
   std::vector<collected_request> requests_;
};

[[nodiscard]] forge::config::value logger_route(std::string name,
                                                std::string level = "info",
                                                bool enabled = true,
                                                bool export_logs = true) {
   auto object = forge::config::value::object_type{};
   object.emplace("name", forge::config::value{std::move(name)});
   object.emplace("level", forge::config::value{std::move(level)});
   object.emplace("enabled", forge::config::value{enabled});
   object.emplace("export", forge::config::value{export_logs});
   return forge::config::value{std::move(object)};
}

[[nodiscard]] forge::config::document plugin_config(const std::string& endpoint,
                                                    forge::config::value::array_type loggers) {
   auto document = forge::config::document{};
   document.set("plugins.log.otlp.endpoint", endpoint);
   document.set("plugins.log.otlp.logs-path", std::string{"/v1/logs"});
   document.set("plugins.log.otlp.protocol", std::string{"http-json"});
   document.set("plugins.log.otlp.loggers", forge::config::value{std::move(loggers)});
   document.set("plugins.log.otlp.batch.flush-interval-ms", std::uint64_t{60000});
   document.set("plugins.log.otlp.retry.max-attempts", std::uint64_t{0});
   return document;
}

struct plugin_harness {
   forge::asio::runtime runtime;
   forge::asio::task_scheduler scheduler;
   forge::api::registry apis;
   forge::app::signal_bus signals;
   forge::app::event_bus events;
   forge::app::diagnostics_store diagnostics;
   log_otlp::plugin plugin;

   plugin_harness() : runtime{}, scheduler{runtime} {}

   void configure(const forge::config::document& document) {
      forge::asio::blocking::run(
         runtime, plugin.configure(forge::config::component_view{document, "plugins.log.otlp"}));
   }

   void provide_and_start() {
      auto provider = forge::api::installer{apis};
      forge::asio::blocking::run(runtime, plugin.provide(provider));
      auto context = forge::app::plugin_context{scheduler, apis, signals, events, &diagnostics};
      forge::asio::blocking::run(runtime, plugin.initialize(context));
      forge::asio::blocking::run(runtime, plugin.startup());
   }

   void shutdown() {
      forge::asio::blocking::run(runtime, plugin.shutdown());
   }
};

void expect_contains(std::string_view haystack, std::string_view needle) {
   BOOST_TEST_CONTEXT("needle: " << needle) {
      BOOST_TEST(haystack.find(needle) != std::string_view::npos);
   }
}

} // namespace

BOOST_AUTO_TEST_SUITE(log_otlp_plugin_test_suite)

BOOST_AUTO_TEST_CASE(log_otlp_descriptor_api_and_config_are_nested) {
   auto plugin = log_otlp::plugin{};
   BOOST_TEST(plugin.id().value == "forge.plugins.log.otlp");
   BOOST_TEST(log_otlp::api::ref().id.value == "forge.plugins.log.otlp");

   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "plugins.log.otlp");
   const auto has = [&](std::string_view field) {
      return std::ranges::any_of(descriptor->fields, [&](const auto& value) { return value.name == field; });
   };
   BOOST_TEST(has("endpoint"));
   BOOST_TEST(has("loggers"));
   BOOST_TEST(has("queue"));
   BOOST_TEST(has("batch"));
   BOOST_TEST(has("retry"));
   BOOST_TEST(has("crash-spool"));
}

BOOST_AUTO_TEST_CASE(log_otlp_disabled_config_does_not_export_and_api_is_unavailable) {
   auto harness = plugin_harness{};
   auto document = forge::config::document{};
   document.set("plugins.log.otlp.enabled", false);
   harness.configure(document);
   harness.provide_and_start();

   auto api = harness.apis.get<log_otlp::api>(log_otlp::api::ref());
   BOOST_CHECK_THROW(forge::asio::blocking::run(harness.runtime, api->metrics()),
                     log_otlp::exceptions::exporter_unavailable);
   BOOST_CHECK_THROW(forge::asio::blocking::run(harness.runtime, api->flush()),
                     log_otlp::exceptions::exporter_unavailable);

   harness.shutdown();
}

BOOST_AUTO_TEST_CASE(log_otlp_exports_default_and_named_logger_routes) {
   forge::configure_logging(forge::logging_config{});

   auto harness = plugin_harness{};
   auto collector = fake_collector{harness.runtime};
   harness.configure(plugin_config(
      collector.endpoint(),
      {logger_route("default"), logger_route("plugin.dynamic", "debug")}));
   harness.provide_and_start();

   ilog("default route exported ${value}", ("value", "one"));
   auto named = forge::logger::get("plugin.dynamic");
   forge_ilog(named, "named route exported ${value}", ("value", "two"));

   auto api = harness.apis.get<log_otlp::api>(log_otlp::api::ref());
   forge::asio::blocking::run(harness.runtime, api->flush());

   BOOST_REQUIRE(collector.wait_for_requests(1));
   const auto requests = collector.requests();
   BOOST_REQUIRE(!requests.empty());
   const auto body = std::accumulate(requests.begin(), requests.end(), std::string{}, [](std::string out, const auto& request) {
      out += request.body;
      return out;
   });
   expect_contains(body, "default route exported one");
   expect_contains(body, "named route exported two");
   expect_contains(body, "\"logger\"");
   expect_contains(body, "default");
   expect_contains(body, "plugin.dynamic");

   const auto snapshot = forge::asio::blocking::run(harness.runtime, api->metrics());
   BOOST_TEST(snapshot.enqueued_records >= 2U);
   BOOST_TEST(snapshot.exported_records >= 2U);

   harness.shutdown();
}

BOOST_AUTO_TEST_CASE(log_otlp_rejects_invalid_config_through_schema_and_domain_validation) {
   {
      auto harness = plugin_harness{};
      auto document = forge::config::document{};
      document.set("plugins.log.otlp.protocol", std::string{"grpc"});
      BOOST_CHECK_THROW(harness.configure(document), log_otlp::exceptions::invalid_config);
   }

   {
      auto harness = plugin_harness{};
      auto document = forge::config::document{};
      document.set("plugins.log.otlp.endpoint", std::string{"not a url"});
      BOOST_CHECK_THROW(harness.configure(document), log_otlp::exceptions::invalid_config);
   }

   {
      auto harness = plugin_harness{};
      auto document = plugin_config("http://localhost:4318", {logger_route("dup"), logger_route("dup")});
      BOOST_CHECK_THROW(harness.configure(document), log_otlp::exceptions::invalid_config);
   }

   {
      auto harness = plugin_harness{};
      auto document = plugin_config("http://localhost:4318", {logger_route("bad\nname")});
      BOOST_CHECK_THROW(harness.configure(document), log_otlp::exceptions::invalid_config);
   }
}

BOOST_AUTO_TEST_SUITE_END()
