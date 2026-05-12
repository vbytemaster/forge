#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <csignal>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import fcl.app;
import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.asio.task_scheduler;
import fcl.config;
import fcl.schema;

namespace {

class status_port {
 public:
   virtual ~status_port() = default;
   virtual std::string status() const = 0;
};

class status_port_impl final : public status_port {
 public:
   std::string status() const override {
      return "ready";
   }
};

class operator_plugin final : public fcl::app::plugin {
 public:
   fcl::app::plugin_id id() const override {
      return {.value = "operator"};
   }

   std::string version() const override {
      return "1";
   }

   std::optional<fcl::config::component_descriptor> describe_config() const override {
      return fcl::config::component_descriptor{
          .section = "operator",
          .fields = {fcl::config::field_descriptor{
              .name = "workers",
              .kind = fcl::schema::value_kind::unsigned_integer,
              .required = true,
              .description = "Number of worker slots owned by the program.",
          }},
      };
   }

   boost::asio::awaitable<void> configure(fcl::config::component_view view) override {
      workers_ = view.get_or<std::uint16_t>("workers", 1);
      co_return;
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      if (workers_ == 0) {
         throw std::runtime_error{"operator workers must be greater than zero"};
      }
      context.ports().install<status_port>(std::make_shared<status_port_impl>());
      context.events().publish(fcl::app::event_severity::info, "operator.initialize", "status port installed");
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   void request_stop() noexcept override {
      stopping_ = true;
   }

   boost::asio::awaitable<void> shutdown() override {
      stopping_ = true;
      co_return;
   }

 private:
   std::uint16_t workers_ = 0;
   bool stopping_ = false;
};

class service_app final : public fcl::app::application_base {
 public:
   service_app()
       : scheduler_{runtime_}, context_{scheduler_, ports_, signals_, events_, &diagnostics_},
         app_{context_, make_plugins(), &diagnostics_} {
      signals_.plugin_started.connect([this](const fcl::app::plugin_signal& signal) {
         events_.publish(fcl::app::event_severity::info, "plugin.started", signal.plugin);
      });
   }

   boost::asio::awaitable<void> initialize() override {
      auto document = fcl::config::document{};
      document.set("operator.workers", 2U);
      co_await app_.configure(document);
      co_await app_.initialize();
   }

   boost::asio::awaitable<void> startup() override {
      co_await app_.startup();
   }

   void request_stop() noexcept override {
      app_.request_stop();
      scheduler_.stop();
   }

   boost::asio::awaitable<void> shutdown() override {
      co_await app_.shutdown();
   }

   fcl::asio::runtime& runtime() noexcept {
      return runtime_;
   }

   const fcl::app::diagnostics_store& diagnostics() const noexcept {
      return diagnostics_;
   }

   fcl::app::event_bus& events() noexcept {
      return events_;
   }

 private:
   static std::vector<std::unique_ptr<fcl::app::plugin>> make_plugins() {
      auto plugins = std::vector<std::unique_ptr<fcl::app::plugin>>{};
      plugins.push_back(std::make_unique<operator_plugin>());
      return plugins;
   }

   fcl::asio::runtime runtime_{{.worker_threads = 1, .thread_name = "example-app"}};
   fcl::asio::task_scheduler scheduler_{runtime_};
   fcl::app::port_registry ports_;
   fcl::app::signal_bus signals_;
   fcl::app::event_bus events_;
   fcl::app::diagnostics_store diagnostics_;
   fcl::app::plugin_context context_;
   fcl::app::application_runtime app_;
};

void install_signal_bridge(service_app& app) {
   auto signals = std::make_shared<boost::asio::signal_set>(app.runtime().context(), SIGINT, SIGTERM);
   boost::asio::co_spawn(
       app.runtime().context(),
       [&app, signals]() -> boost::asio::awaitable<void> {
          co_await signals->async_wait(boost::asio::use_awaitable);
          app.request_stop();
       },
       boost::asio::detached);
}

} // namespace

int main() {
   auto app = service_app{};
   install_signal_bridge(app);
   fcl::asio::blocking::run(app.runtime(), app.initialize());
   fcl::asio::blocking::run(app.runtime(), app.startup());
   app.request_stop();
   fcl::asio::blocking::run(app.runtime(), app.shutdown());

   const auto snapshot = app.diagnostics().snapshot(app.events());
   return snapshot.plugins.empty() ? 1 : 0;
}
