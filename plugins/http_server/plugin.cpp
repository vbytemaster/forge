module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.http_server.plugin;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.asio.runtime;
import fcl.asio.task_scheduler;
import fcl.config.component;
import fcl.config.decode;
import fcl.http.api;
import fcl.http.middleware;
import fcl.http.server;
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.middleware;
import fcl.plugins.http_server.publisher;
import fcl.plugins.http_server.types;

#include "private/config_decode.hxx"
#include "private/publication_store.hxx"
#include "private/server_state.hxx"

namespace fcl::plugins::http_server {

struct plugin::impl {
   config settings;
   detail::publication_store publications;
   std::unique_ptr<detail::server_state> server;
};

class plugin::publisher_impl final : public publisher {
 public:
   explicit publisher_impl(std::shared_ptr<impl> state) : state_{std::move(state)} {}

   void publish(fcl::http::api_binding binding, publish_options options) override {
      state_->publications.publish(std::move(binding), std::move(options));
   }

 private:
   std::shared_ptr<impl> state_;
};

class plugin::middleware_impl final : public middleware {
 public:
   explicit middleware_impl(std::shared_ptr<impl> state) : state_{std::move(state)} {}

   void use(fcl::http::middleware_descriptor descriptor) override {
      state_->publications.use(std::move(descriptor));
   }

 private:
   std::shared_ptr<impl> state_;
};

plugin::plugin() : impl_{std::make_shared<impl>()} {}

plugin::~plugin() = default;

fcl::app::plugin_id plugin::id() const {
   return fcl::app::plugin_id{.value = "fcl.http_server"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> plugin::describe_config() const {
   return fcl::config::describe_component<config>("http-server");
}

boost::asio::awaitable<void> plugin::configure(fcl::config::component_view view) {
   impl_->settings = detail::decode_config(view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(fcl::api::provider& provider) {
   provider.install<publisher>(std::make_shared<publisher_impl>(impl_));
   provider.install<middleware>(std::make_shared<middleware_impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(fcl::app::plugin_context& context) {
   impl_->server = std::make_unique<detail::server_state>(impl_->settings);
   impl_->server->set_runtime(context.scheduler().runtime_context());
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   if (!impl_->server) {
      FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "HTTP server plugin is not initialized");
   }
   co_await impl_->server->start(impl_->publications.close());
}

void plugin::request_stop() noexcept {
   if (impl_ && impl_->server) {
      impl_->server->request_stop();
   }
}

boost::asio::awaitable<void> plugin::shutdown() {
   if (impl_->server) {
      co_await impl_->server->stop();
   }
}

fcl::app::plugin_descriptor descriptor() {
   return fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "fcl.http_server"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace fcl::plugins::http_server
