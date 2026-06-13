module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string>

export module fcl.plugins.signature_provider.plugin;

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.plugins.signature_provider.types;

export namespace fcl::plugins::signature_provider {

class plugin final : public fcl::app::plugin {
 public:
   explicit plugin(plugin_options value = {});
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
   class api_impl;
   std::shared_ptr<impl> impl_;
};

[[nodiscard]] fcl::app::plugin_descriptor descriptor(plugin_options value = {});

} // namespace fcl::plugins::signature_provider
