#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/describe.hpp>

#include <csignal>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

import fcl.app;
import fcl.api;
import fcl.asio.blocking;
import fcl.config;
import fcl.schema;

namespace {

struct service_config {
   std::uint16_t workers = 2;
   bool foreground = true;
};

BOOST_DESCRIBE_STRUCT(service_config, (), (workers, foreground))

struct operator_config {
   std::uint16_t bind_port = 9090;
};

BOOST_DESCRIBE_STRUCT(operator_config, (), (bind_port))

struct status_result {
   std::string value;
};

BOOST_DESCRIBE_STRUCT(status_result, (), (value))

} // namespace

template <>
struct fcl::schema::rules<service_config> {
   static fcl::schema::object_schema<service_config> define() {
      auto schema = fcl::schema::object<service_config>();
      schema.field<&service_config::workers>("workers").default_value(2).range(1, 64);
      schema.field<&service_config::foreground>("foreground").default_value(true);
      return schema;
   }
};

template <>
struct fcl::schema::rules<operator_config> {
   static fcl::schema::object_schema<operator_config> define() {
      auto schema = fcl::schema::object<operator_config>();
      schema.field<&operator_config::bind_port>("bind-port").default_value(9090).range(1, 65'535);
      return schema;
   }
};

namespace {

class status_api {
 public:
   virtual ~status_api() = default;
   virtual boost::asio::awaitable<status_result> status(int request) = 0;

   static fcl::api::descriptor describe() {
      return fcl::api::contract<status_api>({.id = {"status"}, .version = {.major = 1, .revision = 0}})
         .method<&status_api::status, int, status_result>("status")
         .build();
   }
};

class status_api_impl final : public status_api {
 public:
   boost::asio::awaitable<status_result> status(int) override {
      co_return status_result{.value = "ready"};
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
      return fcl::config::describe_component<operator_config>("operator");
   }

   boost::asio::awaitable<void> configure(fcl::config::component_view view) override {
      bind_port_ = view.get_or<std::uint16_t>("bind-port", 9090);
      co_return;
   }

   boost::asio::awaitable<void> provide(fcl::api::provider& provider) override {
      provider.install<status_api>(status_api::describe(), std::make_shared<status_api_impl>());
      co_return;
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      context.events().publish(
         fcl::app::event_severity::info,
         "operator.initialize",
         "status API available on " + std::to_string(bind_port_));
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
   std::uint16_t bind_port_ = 0;
   bool stopping_ = false;
};

class service_application final : public fcl::app::application_shell {
 public:
   service_application()
       : fcl::app::application_shell{fcl::app::application_shell_options{
            .name = "example-app",
            .runtime = {.worker_threads = 1, .thread_name = "example-app"},
         }} {}

   std::uint16_t workers() const noexcept {
      return workers_;
   }

 protected:
   void on_describe_config(fcl::config::component_registry& registry) const override {
      registry.add(fcl::config::describe_component<service_config>("service"));
   }

   boost::asio::awaitable<void> on_configure(fcl::app::configure_context& context) override {
      workers_ = context.view("service").get_or<std::uint16_t>("workers", 2);
      co_return;
   }

   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "operator"},
         .factory = [] {
            return std::make_unique<operator_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(fcl::app::application_context& context) override {
      context.events().publish(
         fcl::app::event_severity::info,
         "service.configure",
         "worker slots: " + std::to_string(workers_));
      co_return;
   }

 private:
   std::uint16_t workers_ = 0;
};

void install_signal_bridge(service_application& app) {
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
   auto app = service_application{};
   install_signal_bridge(app);

   auto document = fcl::config::document{};
   document.set("service.workers", 4U);
   document.set("operator.bind-port", 7777U);

   app.configure(document);
   fcl::asio::blocking::run(app.runtime(), app.startup());
   app.request_stop();
   fcl::asio::blocking::run(app.runtime(), app.shutdown());

   const auto snapshot = app.diagnostics().snapshot(app.events());
   return snapshot.plugins.empty() ? 1 : 0;
}
