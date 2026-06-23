module;

#include <boost/asio/awaitable.hpp>

#include <optional>
#include <string>
#include <compare>

export module forge.app.plugin;

import forge.config.component;
import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.app.plugin_context;

export namespace forge::app {

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
   virtual boost::asio::awaitable<void> provide(forge::api::provider& provider);
   virtual boost::asio::awaitable<void> initialize(plugin_context& context) = 0;
   virtual boost::asio::awaitable<void> startup() = 0;
   virtual void request_stop() noexcept;
   virtual boost::asio::awaitable<void> shutdown() = 0;
};

[[nodiscard]] bool valid_plugin_id(const plugin_id& id) noexcept;

} // namespace forge::app
