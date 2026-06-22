module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <variant>

module fcl.app.application_shell;

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.asio.task_scheduler;
import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;
import fcl.exceptions;
import fcl.schema.value_kind;
import fcl.app.application;
import fcl.app.diagnostics;
import fcl.app.events;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.app.signals;

namespace fcl::app {
namespace {

std::string current_exception_message() {
   try {
      throw;
   } catch (const fcl::exceptions::base& error) {
      return error.message();
   } catch (const std::exception& error) {
      return error.what();
   } catch (...) {
      return "unknown error";
   }
}

void publish_application_event(event_bus& events, event_severity severity, std::string name, std::string transition,
                               std::string message = {}) {
   if (message.empty()) {
      message = transition;
   }
   events.publish(severity, "app." + std::move(name) + "." + std::move(transition), std::move(message));
}

[[nodiscard]] std::vector<plugin_config> enabled_config_for_all_plugins(const plugin_registry& registry) {
   auto out = std::vector<plugin_config>{};
   for (const auto& descriptor : registry.descriptors()) {
      out.push_back(plugin_config{
         .id = descriptor.id,
         .enabled = true,
      });
   }
   return out;
}

[[nodiscard]] std::string plugin_selection_key(const plugin_id& id) {
   constexpr auto official_prefix = std::string_view{"fcl.plugins."};
   if (id.value.starts_with(official_prefix)) {
      return id.value.substr(official_prefix.size());
   }
   return id.value;
}

[[nodiscard]] fcl::config::component_descriptor plugin_selection_descriptor(const plugin_registry& registry) {
   auto descriptor = fcl::config::component_descriptor{.section = "plugins"};
   for (const auto& plugin : registry.descriptors()) {
      const auto key = plugin_selection_key(plugin.id);
      descriptor.fields.push_back(fcl::config::field_descriptor{
         .name = key + ".enabled",
         .kind = fcl::schema::value_kind::boolean,
         .has_default = true,
         .default_value = plugin.enabled_by_default,
         .description = "Enable plugin " + plugin.id.value,
      });
   }
   return descriptor;
}

[[nodiscard]] std::vector<plugin_config> plugin_selection_from_document(const plugin_registry& registry,
                                                                        const fcl::config::document& document) {
   auto out = std::vector<plugin_config>{};
   for (const auto& descriptor : registry.descriptors()) {
      auto enabled = descriptor.enabled_by_default;
      const auto path = "plugins." + plugin_selection_key(descriptor.id) + ".enabled";
      if (const auto* configured = document.try_get(path)) {
         if (const auto* value = std::get_if<bool>(&configured->storage)) {
            enabled = *value;
         } else if (const auto* text = std::get_if<std::string>(&configured->storage)) {
            auto parsed = false;
            if (!fcl::config::parse_bool_text(*text, parsed)) {
               throw std::invalid_argument{"plugin enabled flag must be boolean: " + path};
            }
            enabled = parsed;
         } else {
            throw std::invalid_argument{"plugin enabled flag must be boolean: " + path};
         }
      }
      out.push_back(plugin_config{
         .id = descriptor.id,
         .enabled = enabled,
      });
   }
   return out;
}

} // namespace

application_context::application_context(fcl::asio::runtime& runtime, fcl::asio::task_scheduler& scheduler,
                                         fcl::api::registry& apis, signal_bus& signals,
                                         event_bus& events, diagnostics_store& diagnostics)
    : runtime_{&runtime}, scheduler_{&scheduler}, apis_{&apis}, signals_{&signals},
      events_{&events}, diagnostics_{&diagnostics} {}

fcl::asio::runtime& application_context::runtime() noexcept {
   return *runtime_;
}

fcl::asio::task_scheduler& application_context::scheduler() noexcept {
   return *scheduler_;
}

fcl::api::installer application_context::apis() noexcept {
   return fcl::api::installer{*apis_};
}

signal_bus& application_context::signals() noexcept {
   return *signals_;
}

event_bus& application_context::events() noexcept {
   return *events_;
}

diagnostics_store& application_context::diagnostics() noexcept {
   return *diagnostics_;
}

configure_context::configure_context(const fcl::config::document& document) : document_{&document} {}

const fcl::config::document& configure_context::document() const noexcept {
   return *document_;
}

fcl::config::component_view configure_context::view(std::string section) const {
   return fcl::config::component_view{*document_, std::move(section)};
}

struct application_shell::impl {
   explicit impl(application_shell_options input)
       : options{std::move(input)}, runtime{options.runtime}, scheduler{runtime, options.scheduler},
         context{runtime, scheduler, apis, signals, events, diagnostics} {}

   void require_created(const char* operation) const {
      if (state != application_state::created) {
         throw std::logic_error{std::string{"application shell cannot "} + operation + " after initialize"};
      }
   }

   application_shell_options options;
   fcl::asio::runtime runtime;
   fcl::asio::task_scheduler scheduler;
   fcl::api::registry apis;
   signal_bus signals;
   event_bus events;
   diagnostics_store diagnostics;
   application_context context;
   plugin_registry registry;
   std::unique_ptr<plugin_context> plugin_context_value;
   std::unique_ptr<application_runtime> plugin_runtime;
   fcl::config::document effective_config;
   bool plugins_registered = false;
   bool configured = false;
   bool apis_provided = false;
   application_state state = application_state::created;
};

application_shell::application_shell(application_shell_options options) : impl_{std::make_unique<impl>(std::move(options))} {}

application_shell::~application_shell() = default;

void application_shell::on_describe_config(fcl::config::component_registry&) const {}

boost::asio::awaitable<void> application_shell::on_configure(configure_context&) {
   co_return;
}

void application_shell::on_register_plugins(plugin_registry&) {}

boost::asio::awaitable<void> application_shell::on_provide(application_context&) {
   co_return;
}

int application_shell::on_run_foreground() {
   return 0;
}

void application_shell::ensure_plugins_registered() {
   if (impl_->plugins_registered) {
      return;
   }
   on_register_plugins(impl_->registry);
   impl_->plugins_registered = true;
}

void application_shell::instantiate_plugins(const fcl::config::document& document) {
   ensure_plugins_registered();
   impl_->plugin_runtime.reset();
   impl_->plugin_context_value.reset();
   impl_->plugin_context_value =
       std::make_unique<plugin_context>(impl_->scheduler, impl_->apis, impl_->signals, impl_->events, &impl_->diagnostics);
   impl_->plugin_runtime = std::make_unique<application_runtime>(
      *impl_->plugin_context_value,
      impl_->registry.instantiate_enabled(plugin_selection_from_document(impl_->registry, document)),
      &impl_->diagnostics);
}

fcl::config::component_registry application_shell::collect_config() {
   ensure_plugins_registered();
   auto registry = fcl::config::component_registry{};
   on_describe_config(registry);
   if (!impl_->registry.descriptors().empty()) {
      registry.add(plugin_selection_descriptor(impl_->registry));
   }

   auto plugin_context_value =
       plugin_context{impl_->scheduler, impl_->apis, impl_->signals, impl_->events, &impl_->diagnostics};
   auto plugin_runtime = application_runtime{
      plugin_context_value,
      impl_->registry.instantiate_enabled(enabled_config_for_all_plugins(impl_->registry)),
      &impl_->diagnostics};
   for (auto descriptor : plugin_runtime.describe_config().components()) {
      registry.add(std::move(descriptor));
   }
   return registry;
}

fcl::config::document application_shell::make_effective_config(const fcl::config::document& document) {
   auto registry = collect_config();
   return fcl::config::merge({fcl::config::defaults_for(registry), document});
}

boost::asio::awaitable<void> application_shell::apply_effective_config(fcl::config::document document) {
   impl_->effective_config = std::move(document);
   instantiate_plugins(impl_->effective_config);
   auto context = configure_context{impl_->effective_config};
   co_await on_configure(context);
   co_await impl_->plugin_runtime->configure(impl_->effective_config);
   impl_->configured = true;
}

fcl::config::component_registry application_shell::describe_config() {
   return collect_config();
}

void application_shell::configure(const fcl::config::document& document) {
   impl_->require_created("configure");
   fcl::asio::blocking::run(impl_->runtime, apply_effective_config(make_effective_config(document)));
}

boost::asio::awaitable<void> application_shell::initialize() {
   if (impl_->state != application_state::created) {
      co_return;
   }
   if (!impl_->configured) {
      co_await apply_effective_config(make_effective_config(fcl::config::document{}));
   }
   try {
      impl_->diagnostics.set_application_state(lifecycle_state::initializing, "initialize");
      impl_->signals.application_initializing(application_signal{.name = impl_->options.name});
      publish_application_event(impl_->events, event_severity::info, impl_->options.name, "initializing");
      if (!impl_->apis_provided) {
         co_await on_provide(impl_->context);
         auto provider = impl_->context.apis();
         co_await impl_->plugin_runtime->provide(provider);
         impl_->apis_provided = true;
      }
      co_await impl_->plugin_runtime->initialize();
      impl_->state = application_state::initialized;
      impl_->diagnostics.set_application_state(lifecycle_state::initialized, "initialize");
      impl_->signals.application_initialized(application_signal{.name = impl_->options.name});
      publish_application_event(impl_->events, event_severity::info, impl_->options.name, "initialized");
   } catch (...) {
      const auto message = current_exception_message();
      impl_->state = application_state::stopped;
      impl_->diagnostics.set_application_state(lifecycle_state::failed, "initialize", message);
      publish_application_event(impl_->events, event_severity::error, impl_->options.name, "failed", message);
      impl_->scheduler.stop();
      throw;
   }
}

boost::asio::awaitable<void> application_shell::startup() {
   if (impl_->state == application_state::stopped) {
      throw std::logic_error{"application shell cannot startup after shutdown"};
   }
   if (impl_->state == application_state::created) {
      co_await initialize();
   }
   if (impl_->state == application_state::started) {
      co_return;
   }
   auto failure = std::exception_ptr{};
   try {
      impl_->diagnostics.set_application_state(lifecycle_state::starting, "startup");
      impl_->signals.application_starting(application_signal{.name = impl_->options.name});
      publish_application_event(impl_->events, event_severity::info, impl_->options.name, "starting");
      co_await impl_->plugin_runtime->startup();
      impl_->state = application_state::started;
      impl_->diagnostics.set_application_state(lifecycle_state::started, "startup");
      impl_->signals.application_started(application_signal{.name = impl_->options.name});
      publish_application_event(impl_->events, event_severity::info, impl_->options.name, "started");
   } catch (...) {
      const auto message = current_exception_message();
      impl_->diagnostics.set_application_state(lifecycle_state::failed, "startup", message);
      publish_application_event(impl_->events, event_severity::error, impl_->options.name, "failed", message);
      failure = std::current_exception();
   }
   if (failure) {
      co_await shutdown();
      try {
         std::rethrow_exception(failure);
      } catch (...) {
         impl_->diagnostics.set_application_state(lifecycle_state::failed, "startup", current_exception_message());
      }
      std::rethrow_exception(failure);
   }
}

boost::asio::awaitable<void> application_shell::shutdown() {
   if (impl_->state == application_state::stopped) {
      co_return;
   }
   impl_->diagnostics.set_application_state(lifecycle_state::stopping, "shutdown");
   impl_->signals.application_stopping(application_signal{.name = impl_->options.name});
   publish_application_event(impl_->events, event_severity::info, impl_->options.name, "stopping");
   if (impl_->plugin_runtime) {
      impl_->plugin_runtime->request_stop();
      co_await impl_->plugin_runtime->shutdown();
   }
   impl_->state = application_state::stopped;
   impl_->diagnostics.set_application_state(lifecycle_state::stopped, "shutdown");
   impl_->signals.application_stopped(application_signal{.name = impl_->options.name});
   publish_application_event(impl_->events, event_severity::info, impl_->options.name, "stopped");
   impl_->scheduler.stop();
}

void application_shell::request_stop() noexcept {
   if (impl_->plugin_runtime) {
      impl_->plugin_runtime->request_stop();
   }
}

int application_shell::run() {
   return on_run_foreground();
}

application_state application_shell::state() const noexcept {
   return impl_->state;
}

fcl::asio::runtime& application_shell::runtime() noexcept {
   return impl_->runtime;
}

fcl::asio::task_scheduler& application_shell::scheduler() noexcept {
   return impl_->scheduler;
}

fcl::api::registry& application_shell::apis() noexcept {
   return impl_->apis;
}

signal_bus& application_shell::signals() noexcept {
   return impl_->signals;
}

event_bus& application_shell::events() noexcept {
   return impl_->events;
}

diagnostics_store& application_shell::diagnostics() noexcept {
   return impl_->diagnostics;
}

} // namespace fcl::app
