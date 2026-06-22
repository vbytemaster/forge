module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string>

export module fcl.plugins.http.server.plugin;

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
import fcl.config.component;

export namespace fcl::plugins::http::server {

class plugin final : public fcl::app::plugin {
 public:
   plugin();
   ~plugin() override;

   plugin(const plugin&) = delete;
   plugin& operator=(const plugin&) = delete;

   [[nodiscard]] fcl::app::plugin_id id() const override;
   [[nodiscard]] std::string version() const override;
   [[nodiscard]] std::optional<fcl::config::component_descriptor> describe_config() const override;
   boost::asio::awaitable<void> configure(fcl::config::component_view view) override;
   boost::asio::awaitable<void> provide(fcl::api::provider& provider) override;
   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override;
   boost::asio::awaitable<void> startup() override;
   void request_stop() noexcept override;
   boost::asio::awaitable<void> shutdown() override;

 private:
   struct impl;
   class publisher_api;

   friend boost::asio::awaitable<void> start_server(impl&);
   friend boost::asio::awaitable<void> stop_server(impl&);
   friend void request_server_stop(impl&) noexcept;

   std::shared_ptr<impl> impl_;
};

[[nodiscard]] fcl::app::plugin_descriptor descriptor();

} // namespace fcl::plugins::http::server
