module;

#include <boost/asio/awaitable.hpp>

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module forge.app.application;

import forge.config.component;
import forge.config.document;
import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.exceptions;
import forge.app.events;
import forge.app.signals;

namespace forge::app {
namespace {

std::string exception_message() {
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

void publish_lifecycle_event(plugin_context* context, event_severity severity, const plugin_id& id,
                             const char* transition, std::string message = {}) {
   if (message.empty()) {
      message = transition;
   }
   context->events().publish(severity, "app.plugin." + id.value + "." + transition, std::move(message));
}

} // namespace

application_runtime::application_runtime(plugin_context& context, std::vector<std::unique_ptr<plugin>> plugins,
                                         diagnostics_store* diagnostics)
    : context_{&context}, diagnostics_{diagnostics}, plugins_{std::move(plugins)} {}

application_runtime::~application_runtime() {
   request_stop();
}

config::component_registry application_runtime::describe_config() const {
   auto registry = config::component_registry{};
   for (const auto& value : plugins_) {
      if (auto descriptor = value->describe_config()) {
         registry.add(std::move(*descriptor));
      }
   }
   return registry;
}

boost::asio::awaitable<void> application_runtime::configure(const config::document& document) {
   if (state_ != application_state::created) {
      throw std::logic_error{"app runtime must be configured before initialize"};
   }

   for (auto& value : plugins_) {
      auto section = value->id().value;
      if (auto descriptor = value->describe_config()) {
         section = descriptor->section;
      }
      co_await value->configure(config::component_view{document, std::move(section)});
   }
}

boost::asio::awaitable<void> application_runtime::provide(forge::api::provider& provider) {
   if (state_ != application_state::created) {
      throw std::logic_error{"app runtime must provide APIs before initialize"};
   }

   for (auto& value : plugins_) {
      const auto id = value->id();
      const auto version = value->version();
      try {
         publish_lifecycle_event(context_, event_severity::info, id, "providing");
         co_await value->provide(provider);
         publish_lifecycle_event(context_, event_severity::info, id, "provided");
      } catch (...) {
         const auto message = exception_message();
         if (diagnostics_) {
            diagnostics_->set_plugin_state(id.value, version, lifecycle_state::failed, "provide", message);
         }
         publish_lifecycle_event(context_, event_severity::error, id, "failed", message);
         throw;
      }
   }
}

boost::asio::awaitable<void> application_runtime::initialize() {
   if (state_ != application_state::created) {
      co_return;
   }

   auto failure = std::exception_ptr{};
   try {
      for (auto& value : plugins_) {
         const auto id = value->id();
         const auto version = value->version();
         try {
            if (diagnostics_) {
               diagnostics_->set_plugin_state(id.value, version, lifecycle_state::initializing, "initialize");
            }
            publish_lifecycle_event(context_, event_severity::info, id, "initializing");
            context_->signals().plugin_initializing(plugin_signal{.plugin = id.value});
            co_await value->initialize(*context_);
            ++initialized_count_;
            if (diagnostics_) {
               diagnostics_->set_plugin_state(id.value, version, lifecycle_state::initialized, "initialize");
            }
            context_->signals().plugin_initialized(plugin_signal{.plugin = id.value});
            publish_lifecycle_event(context_, event_severity::info, id, "initialized");
         } catch (...) {
            const auto message = exception_message();
            if (diagnostics_) {
               diagnostics_->set_plugin_state(id.value, version, lifecycle_state::failed, "initialize", message);
            }
            publish_lifecycle_event(context_, event_severity::error, id, "failed", message);
            throw;
         }
      }
      state_ = application_state::initialized;
   } catch (...) {
      if (diagnostics_) {
         diagnostics_->set_application_state(lifecycle_state::failed, "initialize", exception_message());
      }
      failure = std::current_exception();
   }

   if (failure) {
      co_await shutdown();
      std::rethrow_exception(failure);
   }
}

boost::asio::awaitable<void> application_runtime::startup() {
   if (state_ == application_state::stopped) {
      throw std::logic_error{"app runtime cannot startup after shutdown"};
   }
   if (state_ == application_state::created) {
      co_await initialize();
   }
   if (state_ == application_state::started) {
      co_return;
   }

   auto has_startup_failure = false;
   auto failed_plugin_id = std::string{};
   auto failed_plugin_version = std::string{};
   auto failed_plugin_message = std::string{};
   auto failure = std::exception_ptr{};
   try {
      for (auto& value : plugins_) {
         const auto id = value->id();
         const auto version = value->version();
         try {
            if (diagnostics_) {
               diagnostics_->set_plugin_state(id.value, version, lifecycle_state::starting, "startup");
            }
            publish_lifecycle_event(context_, event_severity::info, id, "starting");
            context_->signals().plugin_starting(plugin_signal{.plugin = id.value});
            co_await value->startup();
            ++started_count_;
            if (diagnostics_) {
               diagnostics_->set_plugin_state(id.value, version, lifecycle_state::started, "startup");
            }
            context_->signals().plugin_started(plugin_signal{.plugin = id.value});
            publish_lifecycle_event(context_, event_severity::info, id, "started");
         } catch (...) {
            const auto message = exception_message();
            has_startup_failure = true;
            failed_plugin_id = id.value;
            failed_plugin_version = version;
            failed_plugin_message = message;
            if (diagnostics_) {
               diagnostics_->set_plugin_state(id.value, version, lifecycle_state::failed, "startup", message);
            }
            publish_lifecycle_event(context_, event_severity::error, id, "failed", message);
            throw;
         }
      }
      state_ = application_state::started;
   } catch (...) {
      if (diagnostics_) {
         diagnostics_->set_application_state(lifecycle_state::failed, "startup", exception_message());
      }
      failure = std::current_exception();
   }

   if (failure) {
      co_await shutdown();
      if (diagnostics_ && has_startup_failure) {
         diagnostics_->set_plugin_state(std::move(failed_plugin_id), std::move(failed_plugin_version),
                                        lifecycle_state::failed, "startup", std::move(failed_plugin_message));
      }
      std::rethrow_exception(failure);
   }
}

boost::asio::awaitable<void> application_runtime::shutdown() {
   for (auto index = initialized_count_; index > 0; --index) {
      auto& value = plugins_[index - 1];
      try {
         const auto id = value->id();
         const auto version = value->version();
         if (diagnostics_) {
            diagnostics_->set_plugin_state(id.value, version, lifecycle_state::stopping, "shutdown");
         }
         publish_lifecycle_event(context_, event_severity::info, id, "stopping");
         context_->signals().plugin_stopping(plugin_signal{.plugin = id.value});
         co_await value->shutdown();
         if (diagnostics_) {
            diagnostics_->set_plugin_state(id.value, version, lifecycle_state::stopped, "shutdown");
         }
         context_->signals().plugin_stopped(plugin_signal{.plugin = id.value});
         publish_lifecycle_event(context_, event_severity::info, id, "stopped");
      } catch (...) {
         if (diagnostics_) {
            diagnostics_->set_plugin_state(value->id().value, value->version(), lifecycle_state::failed, "shutdown",
                                           exception_message());
         }
      }
   }
   started_count_ = 0;
   initialized_count_ = 0;
   if (state_ != application_state::created) {
      state_ = application_state::stopped;
   }
}

void application_runtime::request_stop() noexcept {
   for (auto& value : plugins_) {
      value->request_stop();
   }
}

application_state application_runtime::state() const noexcept {
   return state_;
}

std::size_t application_runtime::plugin_count() const noexcept {
   return plugins_.size();
}

} // namespace forge::app
