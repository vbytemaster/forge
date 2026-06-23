module;

#include <boost/asio/awaitable.hpp>

#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module forge.app.application_builder;

import forge.asio.runtime;
import forge.asio.task_scheduler;
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.app.application_shell;
import forge.app.plugin_registry;

namespace forge::app {
namespace {

struct builder_state {
   application_shell_options options;
   std::vector<plugin_descriptor> plugins;
   std::vector<forge::config::component_descriptor> config_descriptors;
   std::vector<std::function<boost::asio::awaitable<void>(configure_context&)>> configure_callbacks;
   std::vector<std::function<boost::asio::awaitable<void>(application_context&)>> provide_callbacks;
   std::function<int(application_shell&)> foreground;
};

class built_application final : public application_shell {
 public:
   explicit built_application(builder_state state) : application_shell{std::move(state.options)}, state_{std::move(state)} {}

 protected:
   void on_describe_config(forge::config::component_registry& registry) const override {
      for (auto descriptor : state_.config_descriptors) {
         registry.add(std::move(descriptor));
      }
   }

   boost::asio::awaitable<void> on_configure(configure_context& context) override {
      for (auto& callback : state_.configure_callbacks) {
         co_await callback(context);
      }
   }

   void on_register_plugins(plugin_registry& registry) override {
      for (auto descriptor : state_.plugins) {
         registry.register_plugin(std::move(descriptor));
      }
   }

   boost::asio::awaitable<void> on_provide(application_context& context) override {
      for (auto& callback : state_.provide_callbacks) {
         co_await callback(context);
      }
   }

   int on_run_foreground() override {
      if (state_.foreground) {
         return state_.foreground(*this);
      }
      return application_shell::on_run_foreground();
   }

 private:
   builder_state state_;
};

} // namespace

struct application_builder::impl {
   builder_state state;
};

application_builder::application_builder() : impl_{std::make_unique<impl>()} {}

application_builder::~application_builder() = default;

application_builder::application_builder(application_builder&&) noexcept = default;

application_builder& application_builder::operator=(application_builder&&) noexcept = default;

application_builder& application_builder::name(std::string value) {
   impl_->state.options.name = std::move(value);
   return *this;
}

application_builder& application_builder::runtime(forge::asio::runtime_options value) {
   impl_->state.options.runtime = std::move(value);
   return *this;
}

application_builder& application_builder::scheduler(forge::asio::task_scheduler::options value) {
   impl_->state.options.scheduler = std::move(value);
   return *this;
}

application_builder& application_builder::plugin(plugin_descriptor descriptor) {
   impl_->state.plugins.push_back(std::move(descriptor));
   return *this;
}

application_builder& application_builder::describe_config(forge::config::component_descriptor descriptor) {
   impl_->state.config_descriptors.push_back(std::move(descriptor));
   return *this;
}

application_builder& application_builder::run_foreground(std::function<int(application_shell&)> handler) {
   impl_->state.foreground = std::move(handler);
   return *this;
}

std::unique_ptr<application_shell> application_builder::build() && {
   if (!impl_) {
      throw std::logic_error{"application builder has already been moved"};
   }
   auto state = std::move(impl_->state);
   impl_.reset();
   return std::make_unique<built_application>(std::move(state));
}

std::invalid_argument application_builder::make_decode_error(const forge::config::decode_diagnostics& diagnostics) {
   auto message = std::ostringstream{};
   message << "application config decode failed";
   for (const auto& entry : diagnostics.entries) {
      message << "; " << entry.path << ": " << entry.message;
   }
   return std::invalid_argument{message.str()};
}

void application_builder::add_configure_callback(configure_callback callback) {
   impl_->state.configure_callbacks.push_back(std::move(callback));
}

void application_builder::add_provide_callback(provide_callback callback) {
   impl_->state.provide_callbacks.push_back(std::move(callback));
}

} // namespace forge::app
