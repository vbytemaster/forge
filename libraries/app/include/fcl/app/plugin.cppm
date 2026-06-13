module;

#include <boost/asio/awaitable.hpp>

#include <optional>
#include <string>
#include <compare>

export module fcl.app.plugin;

import fcl.config.component;
import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.app.plugin_context;

export namespace fcl::app {

enum class plugin_state {
   registered,
   initialized,
   started,
   stopped,
};

struct plugin_id {
   std::string value;

   [[nodiscard]] friend bool operator==(const plugin_id&, const plugin_id&) = default;
   [[nodiscard]] friend auto operator<=>(const plugin_id&, const plugin_id&) = default;
};

struct plugin_config {
   plugin_id id;
   bool enabled = true;
};

class plugin {
 public:
   virtual ~plugin() = default;

   [[nodiscard]] virtual plugin_id id() const = 0;
   [[nodiscard]] virtual std::string version() const = 0;

   [[nodiscard]] virtual std::optional<config::component_descriptor> describe_config() const;
   virtual boost::asio::awaitable<void> configure(config::component_view view);
   virtual boost::asio::awaitable<void> provide(fcl::api::provider& provider);
   virtual boost::asio::awaitable<void> initialize(plugin_context& context) = 0;
   virtual boost::asio::awaitable<void> startup() = 0;
   virtual void request_stop() noexcept;
   virtual boost::asio::awaitable<void> shutdown() = 0;
};

[[nodiscard]] bool valid_plugin_id(const plugin_id& id) noexcept;

} // namespace fcl::app
