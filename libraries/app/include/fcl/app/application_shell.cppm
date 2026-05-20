module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <string>

export module fcl.app.application_shell;

import fcl.asio.runtime;
import fcl.asio.task_scheduler;
import fcl.api;
import fcl.config;
import fcl.app.application;
import fcl.app.diagnostics;
import fcl.app.events;
import fcl.app.plugin_registry;
import fcl.app.signals;

export namespace fcl::app {

struct application_shell_options {
   std::string name = "fcl-app";
   fcl::asio::runtime_options runtime{};
   fcl::asio::task_scheduler_options scheduler{};
};

class application_context {
 public:
   application_context(fcl::asio::runtime& runtime, fcl::asio::task_scheduler& scheduler,
                       fcl::api::registry& apis, signal_bus& signals, event_bus& events,
                       diagnostics_store& diagnostics);

   [[nodiscard]] fcl::asio::runtime& runtime() noexcept;
   [[nodiscard]] fcl::asio::task_scheduler& scheduler() noexcept;
   [[nodiscard]] fcl::api::installer apis() noexcept;
   [[nodiscard]] signal_bus& signals() noexcept;
   [[nodiscard]] event_bus& events() noexcept;
   [[nodiscard]] diagnostics_store& diagnostics() noexcept;

 private:
   fcl::asio::runtime* runtime_ = nullptr;
   fcl::asio::task_scheduler* scheduler_ = nullptr;
   fcl::api::registry* apis_ = nullptr;
   signal_bus* signals_ = nullptr;
   event_bus* events_ = nullptr;
   diagnostics_store* diagnostics_ = nullptr;
};

class configure_context {
 public:
   explicit configure_context(const fcl::config::document& document);

   [[nodiscard]] const fcl::config::document& document() const noexcept;
   [[nodiscard]] fcl::config::component_view view(std::string section) const;

 private:
   const fcl::config::document* document_ = nullptr;
};

class application_shell : public application_base {
 public:
   explicit application_shell(application_shell_options options = {});
   ~application_shell() override;

   application_shell(const application_shell&) = delete;
   application_shell& operator=(const application_shell&) = delete;

   [[nodiscard]] fcl::config::component_registry describe_config();
   void configure(const fcl::config::document& document);
   boost::asio::awaitable<void> initialize() final;
   boost::asio::awaitable<void> startup() final;
   boost::asio::awaitable<void> shutdown() final;
   void request_stop() noexcept final;

   [[nodiscard]] int run();
   [[nodiscard]] application_state state() const noexcept;
   [[nodiscard]] fcl::asio::runtime& runtime() noexcept;
   [[nodiscard]] fcl::asio::task_scheduler& scheduler() noexcept;
   [[nodiscard]] fcl::api::registry& apis() noexcept;
   [[nodiscard]] signal_bus& signals() noexcept;
   [[nodiscard]] event_bus& events() noexcept;
   [[nodiscard]] diagnostics_store& diagnostics() noexcept;

 protected:
   virtual void on_describe_config(fcl::config::component_registry& registry) const;
   virtual boost::asio::awaitable<void> on_configure(configure_context& context);
   virtual void on_register_plugins(plugin_registry& registry);
   virtual boost::asio::awaitable<void> on_provide(application_context& context);
   virtual int on_run_foreground();

 private:
   void ensure_plugins_registered();
   void instantiate_plugins(const fcl::config::document& document);
   [[nodiscard]] fcl::config::component_registry collect_config();
   [[nodiscard]] fcl::config::document make_effective_config(const fcl::config::document& document);
   boost::asio::awaitable<void> apply_effective_config(fcl::config::document document);

   struct impl;
   std::unique_ptr<impl> impl_;
};

} // namespace fcl::app
