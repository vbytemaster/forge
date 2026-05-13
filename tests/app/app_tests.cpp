#include <boost/test/unit_test.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/describe.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

import fcl.app;
import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.asio.task_scheduler;
import fcl.config;
import fcl.schema;

namespace {

struct sample_port {
   virtual ~sample_port() = default;
   virtual int value() const = 0;
};

class sample_port_impl final : public sample_port {
 public:
   explicit sample_port_impl(int value) : value_{value} {}

   int value() const override {
      return value_;
   }

 private:
   int value_ = 0;
};

struct lifecycle_log {
   std::vector<std::string> entries;
};

struct shell_service_config {
   std::uint16_t workers = 2;
};

BOOST_DESCRIBE_STRUCT(shell_service_config, (), (workers))

struct shell_plugin_config {
   std::uint16_t port = 9000;
};

BOOST_DESCRIBE_STRUCT(shell_plugin_config, (), (port))

class test_plugin final : public fcl::app::plugin {
 public:
   test_plugin(std::string id, lifecycle_log& log, bool fail_startup = false)
       : id_{std::move(id)}, log_{&log}, fail_startup_{fail_startup} {}

   fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = id_};
   }
   std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context&) override {
      log_->entries.push_back("initialize:" + id_);
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      log_->entries.push_back("startup:" + id_);
      if (fail_startup_) {
         throw std::runtime_error{"startup failed"};
      }
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      log_->entries.push_back("shutdown:" + id_);
      co_return;
   }

 private:
   std::string id_;
   lifecycle_log* log_ = nullptr;
   bool fail_startup_ = false;
};

class configurable_plugin final : public fcl::app::plugin {
 public:
   configurable_plugin(lifecycle_log& log) : log_{&log} {}

   fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "configurable"};
   }
   std::string version() const override {
      return "1";
   }

   std::optional<fcl::config::component_descriptor> describe_config() const override {
      return fcl::config::component_descriptor{
          .section = "http",
          .fields = {fcl::config::field_descriptor{
              .name = "bind-port",
              .kind = fcl::schema::value_kind::unsigned_integer,
              .required = true,
          }},
      };
   }

   boost::asio::awaitable<void> configure(fcl::config::component_view view) override {
      bind_port_ = view.get_or<std::uint16_t>("bind-port", 0);
      log_->entries.push_back("configure:" + std::to_string(bind_port_));
      co_return;
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context&) override {
      if (bind_port_ == 0) {
         throw std::runtime_error{"not configured"};
      }
      log_->entries.push_back("initialize:configurable");
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      log_->entries.push_back("startup:configurable");
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      log_->entries.push_back("shutdown:configurable");
      co_return;
   }

 private:
   lifecycle_log* log_ = nullptr;
   std::uint16_t bind_port_ = 0;
};

fcl::app::plugin_descriptor descriptor(std::string id, lifecycle_log& log,
                                       std::vector<fcl::app::plugin_id> dependencies = {},
                                       bool enabled_by_default = true, bool fail_startup = false) {
   auto id_value = fcl::app::plugin_id{.value = std::move(id)};
   auto id_copy = id_value;
   return fcl::app::plugin_descriptor{
       .id = std::move(id_value),
       .dependencies = std::move(dependencies),
       .enabled_by_default = enabled_by_default,
       .factory = [id_copy, &log,
                   fail_startup] { return std::make_unique<test_plugin>(id_copy.value, log, fail_startup); },
   };
}

} // namespace

template <>
struct fcl::schema::rules<shell_service_config> {
   static fcl::schema::object_schema<shell_service_config> define() {
      auto schema = fcl::schema::object<shell_service_config>();
      schema.field<&shell_service_config::workers>("workers").default_value(2).range(1, 32);
      return schema;
   }
};

template <>
struct fcl::schema::rules<shell_plugin_config> {
   static fcl::schema::object_schema<shell_plugin_config> define() {
      auto schema = fcl::schema::object<shell_plugin_config>();
      schema.field<&shell_plugin_config::port>("port").default_value(9000).range(1, 65'535);
      return schema;
   }
};

namespace {

class shell_config_plugin final : public fcl::app::plugin {
 public:
   shell_config_plugin(lifecycle_log& log) : log_{&log} {}

   fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "http"};
   }

   std::string version() const override {
      return "1";
   }

   std::optional<fcl::config::component_descriptor> describe_config() const override {
      return fcl::config::describe_component<shell_plugin_config>("http");
   }

   boost::asio::awaitable<void> configure(fcl::config::component_view view) override {
      port_ = view.get_or<std::uint16_t>("port", 0);
      log_->entries.push_back("plugin.configure:" + std::to_string(port_));
      co_return;
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      log_->entries.push_back("plugin.initialize:" + std::to_string(port_));
      context.events().publish(fcl::app::event_severity::info, "shell.plugin", "initialized");
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      log_->entries.push_back("plugin.startup");
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      log_->entries.push_back("plugin.shutdown");
      co_return;
   }

 private:
   lifecycle_log* log_ = nullptr;
   std::uint16_t port_ = 0;
};

class shell_dependency_plugin final : public fcl::app::plugin {
 public:
   shell_dependency_plugin(std::string id, lifecycle_log& log) : id_{std::move(id)}, log_{&log} {}

   fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = id_};
   }

   std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context&) override {
      log_->entries.push_back("initialize:" + id_);
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      log_->entries.push_back("startup:" + id_);
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      log_->entries.push_back("shutdown:" + id_);
      co_return;
   }

 private:
   std::string id_;
   lifecycle_log* log_ = nullptr;
};

class scheduler_cleanup_plugin final : public fcl::app::plugin {
 public:
   explicit scheduler_cleanup_plugin(lifecycle_log& log) : log_{&log} {}

   fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "cleanup"};
   }

   std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      scheduler_ = &context.scheduler();
      log_->entries.push_back("initialize:cleanup");
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      log_->entries.push_back("startup:cleanup");
      co_return;
   }

   void request_stop() noexcept override {
      log_->entries.push_back("request_stop:cleanup");
   }

   boost::asio::awaitable<void> shutdown() override {
      auto handle = scheduler_->submit(fcl::asio::scheduled_task{
         .priority = fcl::asio::priority{1},
         .name = "cleanup-flush",
         .work = [this] {
            log_->entries.push_back("cleanup.scheduler.work");
         },
      });
      co_await handle.wait();
      log_->entries.push_back("shutdown:cleanup");
   }

 private:
   lifecycle_log* log_ = nullptr;
   fcl::asio::task_scheduler* scheduler_ = nullptr;
};

class failing_initialize_plugin final : public fcl::app::plugin {
 public:
   explicit failing_initialize_plugin(lifecycle_log& log) : log_{&log} {}

   fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "init-fail"};
   }

   std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context&) override {
      log_->entries.push_back("initialize:init-fail");
      throw std::runtime_error{"initialize failed"};
   }

   boost::asio::awaitable<void> startup() override {
      log_->entries.push_back("startup:init-fail");
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      log_->entries.push_back("shutdown:init-fail");
      co_return;
   }

 private:
   lifecycle_log* log_ = nullptr;
};

class slow_shutdown_plugin final : public fcl::app::plugin {
 public:
   slow_shutdown_plugin(lifecycle_log& log, bool& shutdown_done) : log_{&log}, shutdown_done_{&shutdown_done} {}

   fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "slow"};
   }

   std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      context_ = &context;
      log_->entries.push_back("initialize:slow");
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      log_->entries.push_back("startup:slow");
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      auto timer = boost::asio::steady_timer{context_->scheduler().runtime_context().context()};
      timer.expires_after(std::chrono::milliseconds{30});
      co_await timer.async_wait(boost::asio::use_awaitable);
      log_->entries.push_back("shutdown:slow");
      *shutdown_done_ = true;
   }

 private:
   lifecycle_log* log_ = nullptr;
   bool* shutdown_done_ = nullptr;
   fcl::app::plugin_context* context_ = nullptr;
};

class shell_test_application final : public fcl::app::application_shell {
 public:
   explicit shell_test_application(lifecycle_log& log)
       : fcl::app::application_shell{fcl::app::application_shell_options{
            .name = "shell-test",
            .runtime = {.worker_threads = 1, .thread_name = "shell-test"},
         }},
         log_{&log} {}

   int run_count() {
      return run();
   }

   std::uint16_t workers() const noexcept {
      return workers_;
   }

 protected:
   void on_describe_config(fcl::config::component_registry& registry) const override {
      registry.add(fcl::config::describe_component<shell_service_config>("service"));
   }

   boost::asio::awaitable<void> on_configure(fcl::app::configure_context& context) override {
      workers_ = context.view("service").get_or<std::uint16_t>("workers", 0);
      log_->entries.push_back("app.configure:" + std::to_string(workers_));
      co_return;
   }

   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "http"},
         .factory = [this] {
            return std::make_unique<shell_config_plugin>(*log_);
         },
      });
   }

   boost::asio::awaitable<void> on_install_ports(fcl::app::application_context& context) override {
      context.ports().install<sample_port>(std::make_shared<sample_port_impl>(workers_));
      log_->entries.push_back("app.install_ports:" + std::to_string(context.ports().size()));
      co_return;
   }

   int on_run_foreground() override {
      log_->entries.push_back("app.run");
      request_stop();
      return 7;
   }

 private:
   lifecycle_log* log_ = nullptr;
   std::uint16_t workers_ = 0;
};

class shell_order_application final : public fcl::app::application_shell {
 public:
   explicit shell_order_application(lifecycle_log& log) : log_{&log} {}

 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "store"},
         .factory = [this] {
            return std::make_unique<shell_dependency_plugin>("store", *log_);
         },
      });
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "api"},
         .dependencies = {fcl::app::plugin_id{.value = "store"}},
         .factory = [this] {
            return std::make_unique<shell_dependency_plugin>("api", *log_);
         },
      });
   }

 private:
   lifecycle_log* log_ = nullptr;
};

class shell_failure_application final : public fcl::app::application_shell {
 public:
   explicit shell_failure_application(lifecycle_log& log) : log_{&log} {}

 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(descriptor("store", *log_));
      registry.register_plugin(descriptor(
         "api",
         *log_,
         {fcl::app::plugin_id{.value = "store"}},
         true,
         true));
   }

 private:
   lifecycle_log* log_ = nullptr;
};

class shell_initialize_failure_application final : public fcl::app::application_shell {
 public:
   explicit shell_initialize_failure_application(lifecycle_log& log) : log_{&log} {}

 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "init-fail"},
         .factory = [this] {
            return std::make_unique<failing_initialize_plugin>(*log_);
         },
      });
   }

 private:
   lifecycle_log* log_ = nullptr;
};

class shell_scheduler_cleanup_application final : public fcl::app::application_shell {
 public:
   explicit shell_scheduler_cleanup_application(lifecycle_log& log) : log_{&log} {}

 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "cleanup"},
         .factory = [this] {
            return std::make_unique<scheduler_cleanup_plugin>(*log_);
         },
      });
   }

 private:
   lifecycle_log* log_ = nullptr;
};

class shell_slow_shutdown_application final : public fcl::app::application_shell {
 public:
   shell_slow_shutdown_application(lifecycle_log& log, bool& shutdown_done)
       : fcl::app::application_shell{fcl::app::application_shell_options{
            .name = "slow-shutdown",
            .runtime = {.worker_threads = 1, .thread_name = "slow-shutdown"},
         }},
         log_{&log},
         shutdown_done_{&shutdown_done} {}

 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "slow"},
         .factory = [this] {
            return std::make_unique<slow_shutdown_plugin>(*log_, *shutdown_done_);
         },
      });
   }

 private:
   lifecycle_log* log_ = nullptr;
   bool* shutdown_done_ = nullptr;
};

class shell_selection_application final : public fcl::app::application_shell {
 public:
   explicit shell_selection_application(lifecycle_log& log) : log_{&log} {}

 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(descriptor("store", *log_));
      registry.register_plugin(descriptor("api", *log_, {fcl::app::plugin_id{.value = "store"}}));
      registry.register_plugin(descriptor("metrics", *log_, {}, false));
   }

 private:
   lifecycle_log* log_ = nullptr;
};

} // namespace

BOOST_AUTO_TEST_CASE(port_registry_installs_gets_and_rejects_duplicates) {
   auto ports = fcl::app::port_registry{};
   BOOST_TEST(ports.size() == 0);
   BOOST_TEST(!ports.try_get<sample_port>());

   ports.install<sample_port>(std::make_shared<sample_port_impl>(7));
   BOOST_TEST(ports.get<sample_port>()->value() == 7);
   BOOST_CHECK_THROW(ports.install<sample_port>(std::make_shared<sample_port_impl>(8)), fcl::app::port_error);

   ports.remove<sample_port>();
   BOOST_TEST(!ports.try_get<sample_port>());
   BOOST_CHECK_THROW(static_cast<void>(ports.get<sample_port>()), fcl::app::port_error);
}

BOOST_AUTO_TEST_CASE(event_bus_bounds_queues_and_keeps_critical_ring) {
   auto bus = fcl::app::event_bus{fcl::app::event_bus_options{
       .max_subscription_events = 1,
       .max_critical_events = 2,
       .max_recent_events = 3,
   }};
   auto subscription = bus.subscribe(fcl::app::event_filter{.topic = "app"});

   bus.publish(fcl::app::event_severity::info, "app.start", "first");
   bus.publish(fcl::app::event_severity::info, "app.start", "dropped");
   bus.publish(fcl::app::event_severity::critical, "system", "critical-a");
   bus.publish(fcl::app::event_severity::critical, "system", "critical-b");
   bus.publish(fcl::app::event_severity::critical, "system", "critical-c");

   BOOST_TEST(bus.metrics().dropped == 1);
   auto first = subscription.poll();
   BOOST_REQUIRE(first.has_value());
   BOOST_TEST(first->message == "first");
   BOOST_TEST(!subscription.poll().has_value());

   const auto critical = bus.critical_events();
   BOOST_REQUIRE_EQUAL(critical.size(), 2);
   BOOST_TEST(critical.front().message == "critical-b");
   BOOST_TEST(critical.back().message == "critical-c");

   const auto recent = bus.recent_events();
   BOOST_REQUIRE_EQUAL(recent.size(), 3);
   BOOST_TEST(recent.front().message == "critical-a");
   BOOST_TEST(recent.back().message == "critical-c");
}

BOOST_AUTO_TEST_CASE(plugin_registry_orders_dependencies_and_rejects_bad_graphs) {
   auto log = lifecycle_log{};
   auto registry = fcl::app::plugin_registry{};
   registry.register_plugin(descriptor("store", log, {}, false));
   registry.register_plugin(descriptor("provider", log, {fcl::app::plugin_id{.value = "store"}}));

   auto plugins = registry.instantiate_enabled({});
   BOOST_REQUIRE_EQUAL(plugins.size(), 2);
   BOOST_TEST(plugins[0]->id().value == "store");
   BOOST_TEST(plugins[1]->id().value == "provider");

   BOOST_CHECK_THROW(registry.register_plugin(descriptor("store", log)), std::invalid_argument);

   auto broken = fcl::app::plugin_registry{};
   broken.register_plugin(descriptor("broken", log, {fcl::app::plugin_id{.value = "missing"}}));
   BOOST_CHECK_THROW(static_cast<void>(broken.instantiate_enabled({})), std::logic_error);

   BOOST_CHECK_THROW(static_cast<void>(registry.instantiate_enabled({fcl::app::plugin_config{
                         .id = fcl::app::plugin_id{.value = "store"},
                         .enabled = false,
                     }})),
                     std::logic_error);
}

BOOST_AUTO_TEST_CASE(application_runtime_rolls_back_and_shutdown_is_idempotent) {
   auto runtime = fcl::asio::runtime{};
   auto scheduler = fcl::asio::task_scheduler{runtime};
   auto ports = fcl::app::port_registry{};
   auto signals = fcl::app::signal_bus{};
   auto events = fcl::app::event_bus{};
   auto context = fcl::app::plugin_context{scheduler, ports, signals, events};
   auto log = lifecycle_log{};

   auto registry = fcl::app::plugin_registry{};
   registry.register_plugin(descriptor("a", log));
   registry.register_plugin(descriptor("b", log, {}, true, true));

   auto app = fcl::app::application_runtime{context, registry.instantiate_enabled({})};
   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime, app.startup()), std::runtime_error);
   BOOST_CHECK(app.state() == fcl::app::application_state::stopped);
   BOOST_REQUIRE_EQUAL(log.entries.size(), 6);
   BOOST_TEST(log.entries[0] == "initialize:a");
   BOOST_TEST(log.entries[1] == "initialize:b");
   BOOST_TEST(log.entries[2] == "startup:a");
   BOOST_TEST(log.entries[3] == "startup:b");
   BOOST_TEST(log.entries[4] == "shutdown:b");
   BOOST_TEST(log.entries[5] == "shutdown:a");

   fcl::asio::blocking::run(runtime, app.shutdown());
   fcl::asio::blocking::run(runtime, app.shutdown());
}

BOOST_AUTO_TEST_CASE(application_runtime_rejects_startup_after_shutdown) {
   auto runtime = fcl::asio::runtime{};
   auto scheduler = fcl::asio::task_scheduler{runtime};
   auto ports = fcl::app::port_registry{};
   auto signals = fcl::app::signal_bus{};
   auto events = fcl::app::event_bus{};
   auto context = fcl::app::plugin_context{scheduler, ports, signals, events};
   auto log = lifecycle_log{};

   auto registry = fcl::app::plugin_registry{};
   registry.register_plugin(descriptor("a", log));

   auto app = fcl::app::application_runtime{context, registry.instantiate_enabled({})};
   fcl::asio::blocking::run(runtime, app.startup());
   fcl::asio::blocking::run(runtime, app.shutdown());

   BOOST_CHECK(app.state() == fcl::app::application_state::stopped);
   BOOST_REQUIRE_EQUAL(log.entries.size(), 3);
   BOOST_TEST(log.entries[0] == "initialize:a");
   BOOST_TEST(log.entries[1] == "startup:a");
   BOOST_TEST(log.entries[2] == "shutdown:a");

   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime, app.startup()), std::logic_error);
   BOOST_CHECK(app.state() == fcl::app::application_state::stopped);
   BOOST_REQUIRE_EQUAL(log.entries.size(), 3);
}

BOOST_AUTO_TEST_CASE(application_runtime_records_diagnostics_and_events) {
   auto runtime = fcl::asio::runtime{};
   auto scheduler = fcl::asio::task_scheduler{runtime};
   auto ports = fcl::app::port_registry{};
   auto signals = fcl::app::signal_bus{};
   auto events = fcl::app::event_bus{};
   auto diagnostics = fcl::app::diagnostics_store{};
   auto context = fcl::app::plugin_context{scheduler, ports, signals, events, &diagnostics};
   auto log = lifecycle_log{};

   auto registry = fcl::app::plugin_registry{};
   registry.register_plugin(descriptor("a", log));

   auto app = fcl::app::application_runtime{context, registry.instantiate_enabled({}), &diagnostics};
   fcl::asio::blocking::run(runtime, app.startup());

   auto snapshot = diagnostics.snapshot(events);
   BOOST_REQUIRE_EQUAL(snapshot.plugins.size(), 1U);
   BOOST_TEST(snapshot.plugins.front().id == "a");
   BOOST_TEST(static_cast<int>(snapshot.plugins.front().state) == static_cast<int>(fcl::app::lifecycle_state::started));
   BOOST_TEST(snapshot.events.published >= 4U);
   BOOST_TEST(!snapshot.recent_events.empty());

   fcl::asio::blocking::run(runtime, app.shutdown());
   snapshot = diagnostics.snapshot(events);
   BOOST_TEST(static_cast<int>(snapshot.plugins.front().state) == static_cast<int>(fcl::app::lifecycle_state::stopped));
}

BOOST_AUTO_TEST_CASE(application_runtime_records_failed_plugin_diagnostics) {
   auto runtime = fcl::asio::runtime{};
   auto scheduler = fcl::asio::task_scheduler{runtime};
   auto ports = fcl::app::port_registry{};
   auto signals = fcl::app::signal_bus{};
   auto events = fcl::app::event_bus{};
   auto diagnostics = fcl::app::diagnostics_store{};
   auto context = fcl::app::plugin_context{scheduler, ports, signals, events, &diagnostics};
   auto log = lifecycle_log{};

   auto registry = fcl::app::plugin_registry{};
   registry.register_plugin(descriptor("a", log, {}, true, true));

   auto app = fcl::app::application_runtime{context, registry.instantiate_enabled({}), &diagnostics};
   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime, app.startup()), std::runtime_error);

   const auto snapshot = diagnostics.snapshot(events);
   BOOST_TEST(static_cast<int>(snapshot.state) == static_cast<int>(fcl::app::lifecycle_state::failed));
   BOOST_REQUIRE_EQUAL(snapshot.plugins.size(), 1U);
   BOOST_TEST(static_cast<int>(snapshot.plugins.front().state) == static_cast<int>(fcl::app::lifecycle_state::failed));
   BOOST_TEST(snapshot.plugins.front().last_error == "startup failed");
}

BOOST_AUTO_TEST_CASE(application_runtime_collects_and_applies_plugin_config_before_initialize) {
   auto runtime = fcl::asio::runtime{};
   auto scheduler = fcl::asio::task_scheduler{runtime};
   auto ports = fcl::app::port_registry{};
   auto signals = fcl::app::signal_bus{};
   auto events = fcl::app::event_bus{};
   auto context = fcl::app::plugin_context{scheduler, ports, signals, events};
   auto log = lifecycle_log{};

   auto plugins = std::vector<std::unique_ptr<fcl::app::plugin>>{};
   plugins.push_back(std::make_unique<configurable_plugin>(log));

   auto app = fcl::app::application_runtime{context, std::move(plugins)};
   const auto registry = app.describe_config();
   BOOST_REQUIRE_EQUAL(registry.components().size(), 1U);
   BOOST_TEST(registry.components().front().section == "http");

   auto document = fcl::config::document{};
   document.set("http.bind-port", 7777);
   fcl::asio::blocking::run(runtime, app.configure(document));
   fcl::asio::blocking::run(runtime, app.startup());
   fcl::asio::blocking::run(runtime, app.shutdown());

   BOOST_REQUIRE_EQUAL(log.entries.size(), 4U);
   BOOST_TEST(log.entries[0] == "configure:7777");
   BOOST_TEST(log.entries[1] == "initialize:configurable");
   BOOST_TEST(log.entries[2] == "startup:configurable");
   BOOST_TEST(log.entries[3] == "shutdown:configurable");
}

BOOST_AUTO_TEST_CASE(application_shell_owns_config_plugin_lifecycle_and_context) {
   auto log = lifecycle_log{};
   auto app = shell_test_application{log};

   const auto registry = app.describe_config();
   BOOST_REQUIRE_EQUAL(registry.components().size(), 3U);
   BOOST_TEST(registry.components()[0].section == "service");
   BOOST_TEST(registry.components()[1].section == "plugins");
   BOOST_TEST(registry.components()[2].section == "http");

   auto document = fcl::config::document{};
   document.set("service.workers", 4);
   app.configure(document);

   fcl::asio::blocking::run(app.runtime(), app.initialize());
   fcl::asio::blocking::run(app.runtime(), app.startup());

   BOOST_TEST(app.workers() == 4U);
   BOOST_TEST(app.ports().get<sample_port>()->value() == 4);

   const auto exit_code = app.run_count();
   BOOST_TEST(exit_code == 7);
   fcl::asio::blocking::run(app.runtime(), app.shutdown());

   const auto expected = std::vector<std::string>{
      "app.configure:4",
      "plugin.configure:9000",
      "app.install_ports:1",
      "plugin.initialize:9000",
      "plugin.startup",
      "app.run",
      "plugin.shutdown",
   };
   BOOST_TEST(log.entries == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(application_shell_preserves_dependency_order_and_reverse_shutdown) {
   auto log = lifecycle_log{};
   auto app = shell_order_application{log};

   app.configure(fcl::config::document{});
   fcl::asio::blocking::run(app.runtime(), app.startup());
   fcl::asio::blocking::run(app.runtime(), app.shutdown());

   const auto expected = std::vector<std::string>{
      "initialize:store",
      "initialize:api",
      "startup:store",
      "startup:api",
      "shutdown:api",
      "shutdown:store",
   };
   BOOST_TEST(log.entries == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(application_shell_rolls_back_started_plugins_on_startup_failure) {
   auto log = lifecycle_log{};
   auto app = shell_failure_application{log};

   BOOST_CHECK_THROW(fcl::asio::blocking::run(app.runtime(), app.startup()), std::runtime_error);
   BOOST_TEST(static_cast<int>(app.state()) == static_cast<int>(fcl::app::application_state::stopped));

   const auto expected = std::vector<std::string>{
      "initialize:store",
      "initialize:api",
      "startup:store",
      "startup:api",
      "shutdown:api",
      "shutdown:store",
   };
   BOOST_TEST(log.entries == expected, boost::test_tools::per_element());

   const auto snapshot = app.diagnostics().snapshot(app.events());
   BOOST_TEST(static_cast<int>(snapshot.state) == static_cast<int>(fcl::app::lifecycle_state::failed));
   BOOST_TEST(snapshot.last_error == "startup failed");
}

BOOST_AUTO_TEST_CASE(application_shell_keeps_scheduler_available_until_shutdown_finishes) {
   auto log = lifecycle_log{};
   auto app = shell_scheduler_cleanup_application{log};

   fcl::asio::blocking::run(app.runtime(), app.startup());
   app.request_stop();
   fcl::asio::blocking::run(app.runtime(), app.shutdown());

   const auto cleanup = std::ranges::find(log.entries, "cleanup.scheduler.work");
   const auto shutdown = std::ranges::find(log.entries, "shutdown:cleanup");
   BOOST_REQUIRE(cleanup != log.entries.end());
   BOOST_REQUIRE(shutdown != log.entries.end());
   BOOST_CHECK(cleanup < shutdown);
}

BOOST_AUTO_TEST_CASE(application_shell_initialize_failure_transitions_to_stopped) {
   auto log = lifecycle_log{};
   auto app = shell_initialize_failure_application{log};

   BOOST_CHECK_THROW(fcl::asio::blocking::run(app.runtime(), app.initialize()), std::runtime_error);
   BOOST_TEST(static_cast<int>(app.state()) == static_cast<int>(fcl::app::application_state::stopped));
   BOOST_CHECK_THROW(fcl::asio::blocking::run(app.runtime(), app.startup()), std::logic_error);

   const auto expected = std::vector<std::string>{
      "initialize:init-fail",
   };
   BOOST_TEST(log.entries == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(application_shell_applies_plugin_selection_from_config) {
   auto log = lifecycle_log{};
   auto app = shell_selection_application{log};

   const auto registry = app.describe_config();
   auto found_plugins_section = false;
   for (const auto& component : registry.components()) {
      if (component.section == "plugins") {
         found_plugins_section = true;
         BOOST_REQUIRE_EQUAL(component.fields.size(), 3U);
         BOOST_TEST(component.fields[0].name == "store.enabled");
         BOOST_TEST(component.fields[1].name == "api.enabled");
         BOOST_TEST(component.fields[2].name == "metrics.enabled");
         BOOST_TEST(component.fields[0].has_default);
         BOOST_TEST(std::get<bool>(component.fields[0].default_value.storage));
         BOOST_TEST(!std::get<bool>(component.fields[2].default_value.storage));
      }
   }
   BOOST_TEST(found_plugins_section);

   auto document = fcl::config::document{};
   document.set("plugins.api.enabled", false);
   document.set("plugins.metrics.enabled", true);
   app.configure(document);
   fcl::asio::blocking::run(app.runtime(), app.startup());
   fcl::asio::blocking::run(app.runtime(), app.shutdown());

   const auto expected = std::vector<std::string>{
      "initialize:store",
      "initialize:metrics",
      "startup:store",
      "startup:metrics",
      "shutdown:metrics",
      "shutdown:store",
   };
   BOOST_TEST(log.entries == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(application_shell_accepts_textual_plugin_selection_flags) {
   auto log = lifecycle_log{};
   auto app = shell_selection_application{log};

   auto document = fcl::config::document{};
   document.set("plugins.api.enabled", "false");
   document.set("plugins.metrics.enabled", "true");
   app.configure(document);
   fcl::asio::blocking::run(app.runtime(), app.startup());
   fcl::asio::blocking::run(app.runtime(), app.shutdown());

   const auto expected = std::vector<std::string>{
      "initialize:store",
      "initialize:metrics",
      "startup:store",
      "startup:metrics",
      "shutdown:metrics",
      "shutdown:store",
   };
   BOOST_TEST(log.entries == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(application_shell_rejects_invalid_textual_plugin_selection_flag) {
   auto log = lifecycle_log{};
   auto app = shell_selection_application{log};

   auto document = fcl::config::document{};
   document.set("plugins.api.enabled", "definitely");

   BOOST_CHECK_THROW(app.configure(document), std::invalid_argument);
   BOOST_TEST(log.entries.empty());
}

BOOST_AUTO_TEST_CASE(application_shell_rejects_enabled_plugin_with_disabled_dependency) {
   auto log = lifecycle_log{};
   auto app = shell_selection_application{log};

   auto document = fcl::config::document{};
   document.set("plugins.store.enabled", false);
   document.set("plugins.api.enabled", true);

   BOOST_CHECK_THROW(app.configure(document), std::logic_error);
   BOOST_TEST(log.entries.empty());
}

BOOST_AUTO_TEST_CASE(run_application_executes_lifecycle_and_custom_stop_waiter) {
   auto log = lifecycle_log{};
   auto app = shell_order_application{log};

   auto options = fcl::app::run_options{};
   options.handle_sigint = false;
   options.handle_sigterm = false;
   options.wait_for_stop = [](fcl::app::application_shell& shell) -> boost::asio::awaitable<void> {
      auto timer = boost::asio::steady_timer{shell.runtime().context()};
      timer.expires_after(std::chrono::milliseconds{5});
      co_await timer.async_wait(boost::asio::use_awaitable);
      co_return;
   };

   const auto exit_code = fcl::app::run_application(app, fcl::config::document{}, options);
   BOOST_TEST(exit_code == 0);
   BOOST_TEST(static_cast<int>(app.state()) == static_cast<int>(fcl::app::application_state::stopped));

   const auto expected = std::vector<std::string>{
      "initialize:store",
      "initialize:api",
      "startup:store",
      "startup:api",
      "shutdown:api",
      "shutdown:store",
   };
   BOOST_TEST(log.entries == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(run_application_unique_ptr_reports_shutdown_timeout_after_cleanup_finishes) {
   auto log = lifecycle_log{};
   auto shutdown_done = false;

   auto app = std::make_unique<shell_slow_shutdown_application>(log, shutdown_done);
   auto options = fcl::app::run_options{};
   options.handle_sigint = false;
   options.handle_sigterm = false;
   options.shutdown_timeout = std::chrono::milliseconds{1};

   BOOST_CHECK_THROW(fcl::app::run_application(std::move(app), fcl::config::document{}, options), std::runtime_error);
   BOOST_TEST(shutdown_done);

   const auto expected = std::vector<std::string>{
      "initialize:slow",
      "startup:slow",
      "shutdown:slow",
   };
   BOOST_TEST(log.entries == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(application_builder_creates_shell_and_applies_config_handlers) {
   auto log = lifecycle_log{};
   auto workers = std::uint16_t{0};
   auto async_config_called = false;

   auto builder = fcl::app::application_builder{};
   builder.name("builder-test")
      .runtime(fcl::asio::runtime_options{.worker_threads = 1, .thread_name = "builder-test"})
      .config<shell_service_config>("service", [&](fcl::app::configure_context& context, const shell_service_config& config)
                                    -> boost::asio::awaitable<void> {
         workers = config.workers;
         log.entries.push_back("typed.configure:" + std::to_string(workers));
         BOOST_TEST(context.view("service").get_or<std::uint16_t>("workers", 0) == 6U);
         co_return;
      })
      .configure([&](fcl::app::configure_context&) {
         async_config_called = true;
         log.entries.push_back("configure.extra");
      })
      .install_ports([&](fcl::app::application_context& context) {
         context.ports().install<sample_port>(std::make_shared<sample_port_impl>(workers));
         log.entries.push_back("install_ports:" + std::to_string(context.ports().size()));
      })
      .run_foreground([&](fcl::app::application_shell& shell) {
         log.entries.push_back("run");
         return shell.ports().get<sample_port>()->value();
      });

   auto app = std::move(builder).build();
   const auto registry = app->describe_config();
   BOOST_REQUIRE_EQUAL(registry.components().size(), 1U);
   BOOST_TEST(registry.components()[0].section == "service");

   auto document = fcl::config::document{};
   document.set("service.workers", 6);
   app->configure(document);
   fcl::asio::blocking::run(app->runtime(), app->startup());

   BOOST_TEST(async_config_called);
   BOOST_TEST(workers == 6U);
   BOOST_TEST(app->ports().get<sample_port>()->value() == 6);
   BOOST_TEST(app->run() == 6);

   fcl::asio::blocking::run(app->runtime(), app->shutdown());

   const auto expected = std::vector<std::string>{
      "typed.configure:6",
      "configure.extra",
      "install_ports:1",
      "run",
   };
   BOOST_TEST(log.entries == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(application_builder_collects_plugin_config_and_preserves_dependency_order) {
   auto log = lifecycle_log{};
   auto builder = fcl::app::application_builder{};
   builder.plugin(fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "http"},
      .factory = [&log] {
         return std::make_unique<shell_config_plugin>(log);
      },
   });
   builder.plugin(fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "store"},
      .factory = [&log] {
         return std::make_unique<shell_dependency_plugin>("store", log);
      },
   });
   builder.plugin(fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "api"},
      .dependencies = {fcl::app::plugin_id{.value = "store"}},
      .factory = [&log] {
         return std::make_unique<shell_dependency_plugin>("api", log);
      },
   });

   auto app = std::move(builder).build();
   const auto registry = app->describe_config();
   BOOST_REQUIRE_EQUAL(registry.components().size(), 2U);
   BOOST_TEST(registry.components()[0].section == "plugins");
   BOOST_TEST(registry.components()[1].section == "http");

   app->configure(fcl::config::document{});
   fcl::asio::blocking::run(app->runtime(), app->startup());
   fcl::asio::blocking::run(app->runtime(), app->shutdown());

   const auto expected = std::vector<std::string>{
      "plugin.configure:9000",
      "plugin.initialize:9000",
      "initialize:store",
      "initialize:api",
      "plugin.startup",
      "startup:store",
      "startup:api",
      "shutdown:api",
      "shutdown:store",
      "plugin.shutdown",
   };
   BOOST_TEST(log.entries == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(application_builder_rejects_invalid_typed_config_before_side_effects) {
   auto configured = false;
   auto ports_installed = false;

   auto builder = fcl::app::application_builder{};
   builder.config<shell_service_config>("service", [&](const shell_service_config&) {
      configured = true;
   });
   builder.install_ports([&](fcl::app::application_context& context) {
      ports_installed = true;
      context.ports().install<sample_port>(std::make_shared<sample_port_impl>(1));
   });

   auto app = std::move(builder).build();
   auto document = fcl::config::document{};
   document.set("service.workers", 99);

   BOOST_CHECK_THROW(app->configure(document), std::invalid_argument);
   BOOST_TEST(!configured);
   BOOST_TEST(!ports_installed);
   BOOST_TEST(!app->ports().try_get<sample_port>());
}
