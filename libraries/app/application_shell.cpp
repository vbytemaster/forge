module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <variant>

module forge.app.application_shell;

import forge.asio.blocking;
import forge.asio.runtime;
import forge.asio.task_scheduler;
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.exceptions;
import forge.schema.value_kind;
import forge.app.application;
import forge.app.diagnostics;
import forge.app.events;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.app.signals;
import forge.app.views;

namespace forge::app {
namespace {

std::string current_exception_message() {
   try {
      throw;
   } catch (const forge::exceptions::base& error) {
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

[[nodiscard]] std::string lifecycle_state_name(lifecycle_state state) {
   switch (state) {
   case lifecycle_state::created:
      return "created";
   case lifecycle_state::initializing:
      return "initializing";
   case lifecycle_state::initialized:
      return "initialized";
   case lifecycle_state::starting:
      return "starting";
   case lifecycle_state::started:
      return "started";
   case lifecycle_state::stopping:
      return "stopping";
   case lifecycle_state::stopped:
      return "stopped";
   case lifecycle_state::failed:
      return "failed";
   }
   return "unknown";
}

[[nodiscard]] view_severity view_severity_from(event_severity severity) {
   switch (severity) {
   case event_severity::debug:
      return view_severity::debug;
   case event_severity::info:
      return view_severity::info;
   case event_severity::warning:
      return view_severity::warning;
   case event_severity::error:
      return view_severity::error;
   case event_severity::critical:
      return view_severity::critical;
   }
   return view_severity::info;
}

[[nodiscard]] std::size_t cursor_offset(std::string_view cursor, std::size_t size) {
   if (cursor.empty()) {
      return 0;
   }
   try {
      const auto parsed = static_cast<std::size_t>(std::stoull(std::string{cursor}));
      return std::min(parsed, size);
   } catch (...) {
      return 0;
   }
}

class diagnostics_view_source final : public view_source {
 public:
   diagnostics_view_source(diagnostics_store& diagnostics, event_bus& events)
       : diagnostics_{&diagnostics}, events_{&events} {}

   boost::asio::awaitable<view_snapshot> snapshot(view_query) override {
      const auto state = diagnostics_->snapshot(*events_);
      co_return view_snapshot{
         .descriptor =
             {
                 .id = "forge.app.status",
                 .title = "Application Status",
                 .category = "app",
                 .kind = view_kind::counters,
             },
         .counters =
             {
                 {.name = "state", .value = lifecycle_state_name(state.state)},
                 {.name = "plugins", .value = std::to_string(state.plugins.size())},
                 {.name = "events.published", .value = std::to_string(state.events.published)},
                 {.name = "events.dropped", .value = std::to_string(state.events.dropped),
                  .severity = state.events.dropped == 0 ? view_severity::info : view_severity::warning},
             },
      };
   }

 private:
   diagnostics_store* diagnostics_ = nullptr;
   event_bus* events_ = nullptr;
};

class recent_events_view_source final : public view_source {
 public:
   explicit recent_events_view_source(event_bus& events) : events_{&events} {}

   boost::asio::awaitable<view_snapshot> snapshot(view_query query) override {
      auto events = events_->recent_events();
      const auto begin = cursor_offset(query.cursor, events.size());
      const auto limit = static_cast<std::size_t>(std::min<std::uint64_t>(query.limit, events.size() - begin));
      auto logs = std::vector<view_log_item>{};
      logs.reserve(limit);
      for (auto index = begin; index < begin + limit; ++index) {
         const auto& event = events[index];
         logs.push_back(view_log_item{
            .id = event.id,
            .severity = view_severity_from(event.severity),
            .topic = event.topic,
            .message = event.message,
         });
      }
      auto page = view_page{};
      if (begin + limit < events.size()) {
         page.next_cursor = std::to_string(begin + limit);
      }
      page.total_estimate = events.size();
      co_return view_snapshot{
         .descriptor =
             {
                 .id = "forge.app.events",
                 .title = "Application Events",
                 .category = "app",
                 .kind = view_kind::log,
             },
         .page = std::move(page),
         .log = std::move(logs),
      };
   }

 private:
   event_bus* events_ = nullptr;
};

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
   constexpr auto official_prefix = std::string_view{"forge.plugins."};
   if (id.value.starts_with(official_prefix)) {
      return id.value.substr(official_prefix.size());
   }
   return id.value;
}

[[nodiscard]] forge::config::component_descriptor plugin_selection_descriptor(const plugin_registry& registry) {
   auto descriptor = forge::config::component_descriptor{.section = "plugins"};
   for (const auto& plugin : registry.descriptors()) {
      const auto key = plugin_selection_key(plugin.id);
      descriptor.fields.push_back(forge::config::field_descriptor{
         .name = key + ".enabled",
         .kind = forge::schema::value_kind::boolean,
         .has_default = true,
         .default_value = plugin.enabled_by_default,
         .description = "Enable plugin " + plugin.id.value,
      });
   }
   return descriptor;
}

[[nodiscard]] std::vector<plugin_config> plugin_selection_from_document(const plugin_registry& registry,
                                                                        const forge::config::document& document) {
   auto out = std::vector<plugin_config>{};
   for (const auto& descriptor : registry.descriptors()) {
      auto enabled = descriptor.enabled_by_default;
      const auto path = "plugins." + plugin_selection_key(descriptor.id) + ".enabled";
      if (const auto* configured = document.try_get(path)) {
         if (const auto* value = std::get_if<bool>(&configured->storage)) {
            enabled = *value;
         } else if (const auto* text = std::get_if<std::string>(&configured->storage)) {
            auto parsed = false;
            if (!forge::config::parse_bool_text(*text, parsed)) {
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

application_context::application_context(forge::asio::runtime& runtime, forge::asio::task_scheduler& scheduler,
                                         forge::api::registry& apis, signal_bus& signals,
                                         event_bus& events, diagnostics_store& diagnostics, view_registry& views)
    : runtime_{&runtime}, scheduler_{&scheduler}, apis_{&apis}, signals_{&signals},
      events_{&events}, diagnostics_{&diagnostics}, views_{&views} {}

forge::asio::runtime& application_context::runtime() noexcept {
   return *runtime_;
}

forge::asio::task_scheduler& application_context::scheduler() noexcept {
   return *scheduler_;
}

forge::api::installer application_context::apis() noexcept {
   return forge::api::installer{*apis_};
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

view_registry& application_context::views() noexcept {
   return *views_;
}

configure_context::configure_context(const forge::config::document& document) : document_{&document} {}

const forge::config::document& configure_context::document() const noexcept {
   return *document_;
}

forge::config::component_view configure_context::view(std::string section) const {
   return forge::config::component_view{*document_, std::move(section)};
}

struct application_shell::impl {
   explicit impl(application_shell_options input)
       : options{std::move(input)}, runtime{options.runtime}, scheduler{runtime, options.scheduler},
         context{runtime, scheduler, apis, signals, events, diagnostics, views} {
      built_in_views.push_back(views.register_source(
         {
            .id = "forge.app.status",
            .title = "Application Status",
            .category = "app",
            .kind = view_kind::counters,
         },
         std::make_shared<diagnostics_view_source>(diagnostics, events)));
      built_in_views.push_back(views.register_source(
         {
            .id = "forge.app.events",
            .title = "Application Events",
            .category = "app",
            .kind = view_kind::log,
         },
         std::make_shared<recent_events_view_source>(events)));
   }

   void require_created(const char* operation) const {
      if (state != application_state::created) {
         throw std::logic_error{std::string{"application shell cannot "} + operation + " after initialize"};
      }
   }

   application_shell_options options;
   forge::asio::runtime runtime;
   forge::asio::task_scheduler scheduler;
   forge::api::registry apis;
   signal_bus signals;
   event_bus events;
   diagnostics_store diagnostics;
   view_registry views;
   application_context context;
   std::vector<view_registration> built_in_views;
   plugin_registry registry;
   std::unique_ptr<plugin_context> plugin_context_value;
   std::unique_ptr<application_runtime> plugin_runtime;
   forge::config::document effective_config;
   bool plugins_registered = false;
   bool configured = false;
   bool apis_provided = false;
   application_state state = application_state::created;
};

application_shell::application_shell(application_shell_options options) : impl_{std::make_unique<impl>(std::move(options))} {}

application_shell::~application_shell() = default;

void application_shell::on_describe_config(forge::config::component_registry&) const {}

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

void application_shell::instantiate_plugins(const forge::config::document& document) {
   ensure_plugins_registered();
   impl_->plugin_runtime.reset();
   impl_->plugin_context_value.reset();
   impl_->plugin_context_value =
       std::make_unique<plugin_context>(impl_->scheduler, impl_->apis, impl_->signals, impl_->events, &impl_->diagnostics,
                                        forge::app::config_view{}, &impl_->views);
   impl_->plugin_runtime = std::make_unique<application_runtime>(
      *impl_->plugin_context_value,
      impl_->registry.instantiate_enabled(plugin_selection_from_document(impl_->registry, document)),
      &impl_->diagnostics);
}

forge::config::component_registry application_shell::collect_config() {
   ensure_plugins_registered();
   auto registry = forge::config::component_registry{};
   on_describe_config(registry);
   if (!impl_->registry.descriptors().empty()) {
      registry.add(plugin_selection_descriptor(impl_->registry));
   }

   auto plugin_context_value =
       plugin_context{impl_->scheduler, impl_->apis, impl_->signals, impl_->events, &impl_->diagnostics,
                      forge::app::config_view{}, &impl_->views};
   auto plugin_runtime = application_runtime{
      plugin_context_value,
      impl_->registry.instantiate_enabled(enabled_config_for_all_plugins(impl_->registry)),
      &impl_->diagnostics};
   for (auto descriptor : plugin_runtime.describe_config().components()) {
      registry.add(std::move(descriptor));
   }
   return registry;
}

forge::config::document application_shell::make_effective_config(const forge::config::document& document) {
   auto registry = collect_config();
   return forge::config::merge({forge::config::defaults_for(registry), document});
}

boost::asio::awaitable<void> application_shell::apply_effective_config(forge::config::document document) {
   impl_->effective_config = std::move(document);
   instantiate_plugins(impl_->effective_config);
   auto context = configure_context{impl_->effective_config};
   co_await on_configure(context);
   co_await impl_->plugin_runtime->configure(impl_->effective_config);
   impl_->configured = true;
}

forge::config::component_registry application_shell::describe_config() {
   return collect_config();
}

void application_shell::configure(const forge::config::document& document) {
   impl_->require_created("configure");
   forge::asio::blocking::run(impl_->runtime, apply_effective_config(make_effective_config(document)));
}

boost::asio::awaitable<void> application_shell::initialize() {
   if (impl_->state != application_state::created) {
      co_return;
   }
   if (!impl_->configured) {
      co_await apply_effective_config(make_effective_config(forge::config::document{}));
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

forge::asio::runtime& application_shell::runtime() noexcept {
   return impl_->runtime;
}

forge::asio::task_scheduler& application_shell::scheduler() noexcept {
   return impl_->scheduler;
}

forge::api::registry& application_shell::apis() noexcept {
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

view_registry& application_shell::views() noexcept {
   return impl_->views;
}

} // namespace forge::app
