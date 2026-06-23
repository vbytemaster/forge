module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string>

export module forge.plugins.p2p.node.plugin;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.config.component;
import forge.plugins.p2p.node.types;

export namespace forge::plugins::p2p::node {

class plugin final : public forge::app::plugin {
 public:
   plugin();
   ~plugin() override;

   plugin(const plugin&) = delete;
   plugin& operator=(const plugin&) = delete;

   [[nodiscard]] forge::app::plugin_id id() const override;
   [[nodiscard]] std::string version() const override;
   [[nodiscard]] std::optional<forge::config::component_descriptor> describe_config() const override;
   boost::asio::awaitable<void> configure(forge::config::component_view view) override;
   boost::asio::awaitable<void> provide(forge::api::provider& provider) override;
   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override;
   boost::asio::awaitable<void> startup() override;
   void request_stop() noexcept override;
   boost::asio::awaitable<void> shutdown() override;

 private:
   struct impl;
   class node_api;
   class diagnostics_source_adapter;
   class pubsub_source_adapter;

   friend void apply_config(impl&, const config&);

   std::shared_ptr<impl> impl_;
};

[[nodiscard]] forge::app::plugin_descriptor descriptor();

} // namespace forge::plugins::p2p::node
