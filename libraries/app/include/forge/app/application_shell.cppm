module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <string>

export module forge.app.application_shell;

import forge.asio.runtime;
import forge.asio.task_scheduler;
import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.app.application;
import forge.app.diagnostics;
import forge.app.events;
import forge.app.plugin_registry;
import forge.app.signals;
import forge.app.views;

export namespace forge::app {

struct application_shell_options {
   std::string name = "forge-app";
   forge::asio::runtime_options runtime{};
   forge::asio::task_scheduler::options scheduler{};
};

class application_context {
 public:
   application_context(forge::asio::runtime& runtime, forge::asio::task_scheduler& scheduler,
                       forge::api::registry& apis, signal_bus& signals, event_bus& events,
                       diagnostics_store& diagnostics, view_registry& views);

   [[nodiscard]] forge::asio::runtime& runtime() noexcept;
   [[nodiscard]] forge::asio::task_scheduler& scheduler() noexcept;
   [[nodiscard]] forge::api::installer apis() noexcept;
   [[nodiscard]] signal_bus& signals() noexcept;
   [[nodiscard]] event_bus& events() noexcept;
   [[nodiscard]] diagnostics_store& diagnostics() noexcept;
   [[nodiscard]] view_registry& views() noexcept;

 private:
   forge::asio::runtime* runtime_ = nullptr;
   forge::asio::task_scheduler* scheduler_ = nullptr;
   forge::api::registry* apis_ = nullptr;
   signal_bus* signals_ = nullptr;
   event_bus* events_ = nullptr;
   diagnostics_store* diagnostics_ = nullptr;
   view_registry* views_ = nullptr;
};

class configure_context {
 public:
   explicit configure_context(const forge::config::document& document);

   [[nodiscard]] const forge::config::document& document() const noexcept;
   [[nodiscard]] forge::config::component_view view(std::string section) const;

 private:
   const forge::config::document* document_ = nullptr;
};

class application_shell : public application_base {
 public:
   explicit application_shell(application_shell_options options = {});
   ~application_shell() override;

   application_shell(const application_shell&) = delete;
   application_shell& operator=(const application_shell&) = delete;

   [[nodiscard]] forge::config::component_registry describe_config();
   void configure(const forge::config::document& document);
   boost::asio::awaitable<void> initialize() final;
   boost::asio::awaitable<void> startup() final;
   boost::asio::awaitable<void> shutdown() final;
   void request_stop() noexcept final;

   [[nodiscard]] int run();
   [[nodiscard]] application_state state() const noexcept;
   [[nodiscard]] forge::asio::runtime& runtime() noexcept;
   [[nodiscard]] forge::asio::task_scheduler& scheduler() noexcept;
   [[nodiscard]] forge::api::registry& apis() noexcept;
   [[nodiscard]] signal_bus& signals() noexcept;
   [[nodiscard]] event_bus& events() noexcept;
   [[nodiscard]] diagnostics_store& diagnostics() noexcept;
   [[nodiscard]] view_registry& views() noexcept;

 protected:
   virtual void on_describe_config(forge::config::component_registry& registry) const;
   virtual boost::asio::awaitable<void> on_configure(configure_context& context);
   virtual void on_register_plugins(plugin_registry& registry);
   virtual boost::asio::awaitable<void> on_provide(application_context& context);
   virtual int on_run_foreground();

 private:
   void ensure_plugins_registered();
   void instantiate_plugins(const forge::config::document& document);
   [[nodiscard]] forge::config::component_registry collect_config();
   [[nodiscard]] forge::config::document make_effective_config(const forge::config::document& document);
   boost::asio::awaitable<void> apply_effective_config(forge::config::document document);

   struct impl;
   std::unique_ptr<impl> impl_;
};

} // namespace forge::app
