#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

import fcl.app;
import fcl.api;
import fcl.asio.blocking;
import fcl.config;
import fcl.schema;

namespace {

struct daemon_service_config {
   std::uint16_t workers = 2;
   std::string token;
};

BOOST_DESCRIBE_STRUCT(daemon_service_config, (), (workers, token))

struct cache_config {
   std::uint32_t read_ahead_blocks = 4;
};

BOOST_DESCRIBE_STRUCT(cache_config, (), (read_ahead_blocks))

struct status_result {
   std::string value;
};

BOOST_DESCRIBE_STRUCT(status_result, (), (value))

} // namespace

template <>
struct fcl::schema::rules<daemon_service_config> {
   static fcl::schema::object_schema<daemon_service_config> define() {
      auto schema = fcl::schema::object<daemon_service_config>();
      schema.field<&daemon_service_config::workers>("workers").default_value(2).range(1, 64);
      schema.field<&daemon_service_config::token>("token").default_value(std::string{}).secret();
      return schema;
   }
};

template <>
struct fcl::schema::rules<cache_config> {
   static fcl::schema::object_schema<cache_config> define() {
      auto schema = fcl::schema::object<cache_config>();
      schema.field<&cache_config::read_ahead_blocks>("read-ahead-blocks").default_value(4).range(0, 128);
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
   explicit status_api_impl(std::filesystem::path data_dir) : data_dir_{std::move(data_dir)} {}

   boost::asio::awaitable<status_result> status(int) override {
      co_return status_result{.value = "ready:" + data_dir_.filename().string()};
   }

 private:
   std::filesystem::path data_dir_;
};

class cache_plugin final : public fcl::app::plugin {
 public:
   fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "cache"};
   }

   std::string version() const override {
      return "1";
   }

   std::optional<fcl::config::component_descriptor> describe_config() const override {
      return fcl::config::describe_component<cache_config>("cache");
   }

   boost::asio::awaitable<void> configure(fcl::config::component_view view) override {
      read_ahead_blocks_ = view.get_or<std::uint32_t>("read-ahead-blocks", 4);
      co_return;
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      context.events().publish(
         fcl::app::event_severity::info,
         "cache.initialize",
         "read-ahead blocks: " + std::to_string(read_ahead_blocks_));
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }

 private:
   std::uint32_t read_ahead_blocks_ = 0;
};

struct daemon_application_options {
   std::filesystem::path data_dir;
   std::string profile;
   fcl::app::application_shell_options shell;
};

class daemon_application final : public fcl::app::application_shell {
 public:
   explicit daemon_application(daemon_application_options options)
       : fcl::app::application_shell{std::move(options.shell)},
         data_dir_{std::move(options.data_dir)},
         profile_{std::move(options.profile)} {}

 protected:
   void on_describe_config(fcl::config::component_registry& registry) const override {
      registry.add(fcl::config::describe_component<daemon_service_config>("service"));
   }

   boost::asio::awaitable<void> on_configure(fcl::app::configure_context& context) override {
      service_ = context.view("service").get_or<std::uint16_t>("workers", 2);
      co_return;
   }

   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "cache"},
         .factory = [] {
            return std::make_unique<cache_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(fcl::app::application_context& context) override {
      context.apis().install<status_api>(status_api::describe(), std::make_shared<status_api_impl>(data_dir_));
      context.events().publish(
         fcl::app::event_severity::info,
         "service.configure",
         profile_ + " workers: " + std::to_string(service_));
      co_return;
   }

   int on_run_foreground() override {
      auto status = apis().get<status_api>({.id = {"status"}, .major = 1});
      return fcl::asio::blocking::run(runtime(), status->status(0)).value.starts_with("ready:") ? 0 : 2;
   }

 private:
   std::filesystem::path data_dir_;
   std::string profile_;
   std::uint16_t service_ = 0;
};

} // namespace

int main(int argc, char** argv) {
   return fcl::app::run_daemon(
      [](const fcl::app::daemon_context& context) {
         return std::make_unique<daemon_application>(daemon_application_options{
            .data_dir = context.data_dir,
            .profile = context.profile,
            .shell = context.shell,
         });
      },
      argc,
      argv,
      fcl::app::daemon_options{
         .name = "daemon-runner-example",
         .display_name = "FCL daemon runner example",
         .default_data_dir_name = "daemon-runner-example",
         .env_prefix = "FCL_DAEMON_EXAMPLE",
         .read_dotenv = false,
         .read_process_env = false,
         .run =
            fcl::app::run_options{
               .handle_sigint = false,
               .handle_sigterm = false,
               .wait_for_stop =
                  [](fcl::app::application_shell&) -> boost::asio::awaitable<void> {
                     co_return;
                  },
            },
      });
}
