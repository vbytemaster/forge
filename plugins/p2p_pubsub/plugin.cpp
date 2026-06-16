module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_pubsub.plugin;

import fcl.api.registry;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.p2p.pubsub;
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_pubsub.api;
import fcl.plugins.p2p_pubsub.types;

#include "details/config.hxx"
#include "details/join_flow.hxx"
#include "details/plugin_impl.hxx"
#include "details/subscription_api.hxx"

namespace fcl::plugins::p2p_pubsub {

plugin::plugin() : impl_{std::make_shared<impl>()} {}
plugin::~plugin() = default;

fcl::app::plugin_id plugin::id() const {
   return fcl::app::plugin_id{.value = "fcl.p2p_pubsub"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> plugin::describe_config() const {
   return fcl::config::describe_component<config>("p2p-pubsub");
}

boost::asio::awaitable<void> plugin::configure(fcl::config::component_view view) {
   auto config = decode_config(view);
   validate_config(config);
   impl_->settings = std::move(config);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(fcl::api::provider& provider) {
   provider.install<api>(std::make_shared<subscription_api>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(fcl::app::plugin_context& context) {
   impl_->source = context.apis()
                      .get<fcl::plugins::p2p_node::pubsub_source>(
                         {.id = {"fcl.plugins.p2p_node.pubsub_source"}, .major = 1, .min_revision = 0})
                      .shared();
   impl_->source->enable(core_options_for(impl_->settings));
   impl_->initialized = true;
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   co_return;
}

void plugin::request_stop() noexcept {
   impl_->stopping = true;
}

boost::asio::awaitable<void> plugin::shutdown() {
   request_stop();
   std::vector<fcl::p2p::pubsub::topic> topics;
   {
      auto lock = std::scoped_lock{impl_->mutex};
      topics.reserve(impl_->topics.size());
      for (const auto& [topic, _] : impl_->topics) {
         topics.push_back(fcl::p2p::pubsub::topic{.value = topic});
      }
      impl_->topics.clear();
   }
   if (impl_->source) {
      for (auto& topic : topics) {
         try {
            co_await impl_->source->async_leave_topic(std::move(topic));
         } catch (...) {
            fcl::exceptions::capture_and_log("P2P PubSub unsubscribe during shutdown failed");
         }
      }
   }
   impl_->initialized = false;
   impl_->source = nullptr;
   co_return;
}

fcl::app::plugin_descriptor descriptor() {
   return fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "fcl.p2p_pubsub"},
      .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_node"}},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace fcl::plugins::p2p_pubsub
