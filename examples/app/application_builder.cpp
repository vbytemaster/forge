#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <cstdint>
#include <memory>
#include <string>

import fcl.app;
import fcl.api;
import fcl.asio.runtime;
import fcl.asio.blocking;
import fcl.config;
import fcl.schema;

namespace {

struct service_config {
   std::uint16_t workers = 2;
};

BOOST_DESCRIBE_STRUCT(service_config, (), (workers))

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
   explicit status_api_impl(std::uint16_t workers) : workers_{workers} {}

   boost::asio::awaitable<status_result> status(int) override {
      co_return status_result{.value = "ready:" + std::to_string(workers_)};
   }

 private:
   std::uint16_t workers_ = 0;
};

class ready_plugin final : public fcl::app::plugin {
 public:
   fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "ready"};
   }

   std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      context.events().publish(fcl::app::event_severity::info, "ready.initialize", "ready plugin initialized");
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

} // namespace

int main() {
   auto workers = std::uint16_t{0};

   auto builder = fcl::app::application_builder{};
   builder.name("builder-example")
      .runtime(fcl::asio::runtime_options{.worker_threads = 1, .thread_name = "builder-example"})
      .config<service_config>("service", [&](const service_config& config) {
         workers = config.workers;
      })
      .provide([&](fcl::app::application_context& context) {
         context.apis().install<status_api>(status_api::describe(), std::make_shared<status_api_impl>(workers));
      })
      .plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "ready"},
         .factory = [] {
            return std::make_unique<ready_plugin>();
         },
      })
      .run_foreground([](fcl::app::application_shell& app) {
         auto status = app.apis().get<status_api>({.id = {"status"}, .major = 1});
         return fcl::asio::blocking::run(app.runtime(), status->status(0)).value == "ready:4" ? 0 : 2;
      });

   auto app = std::move(builder).build();
   auto document = fcl::config::document{};
   document.set("service.workers", 4);

   app->configure(document);
   fcl::asio::blocking::run(app->runtime(), app->startup());
   const auto exit_code = app->run();
   app->request_stop();
   fcl::asio::blocking::run(app->runtime(), app->shutdown());
   return exit_code;
}
