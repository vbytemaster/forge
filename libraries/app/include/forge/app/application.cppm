module;

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <memory>
#include <vector>

export module forge.app.application;

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
import forge.app.diagnostics;
import forge.app.plugin_context;
import forge.app.plugin;

export namespace forge::app {

enum class application_state {
   created,
   initialized,
   started,
   stopped,
};

class application_base {
 public:
   virtual ~application_base() = default;

   application_base(const application_base&) = delete;
   application_base& operator=(const application_base&) = delete;

   virtual boost::asio::awaitable<void> initialize() = 0;
   virtual boost::asio::awaitable<void> startup() = 0;
   virtual boost::asio::awaitable<void> shutdown() = 0;
   virtual void request_stop() noexcept = 0;

 protected:
   application_base() = default;
};

class application_runtime {
 public:
   application_runtime(plugin_context& context, std::vector<std::unique_ptr<plugin>> plugins,
                       diagnostics_store* diagnostics = nullptr);
   ~application_runtime();

   application_runtime(const application_runtime&) = delete;
   application_runtime& operator=(const application_runtime&) = delete;

   [[nodiscard]] config::component_registry describe_config() const;
   boost::asio::awaitable<void> configure(const config::document& document);
   boost::asio::awaitable<void> provide(forge::api::provider& provider);
   boost::asio::awaitable<void> initialize();
   boost::asio::awaitable<void> startup();
   boost::asio::awaitable<void> shutdown();
   void request_stop() noexcept;

   [[nodiscard]] application_state state() const noexcept;
   [[nodiscard]] std::size_t plugin_count() const noexcept;

 private:
   plugin_context* context_ = nullptr;
   diagnostics_store* diagnostics_ = nullptr;
   std::vector<std::unique_ptr<plugin>> plugins_;
   std::size_t initialized_count_ = 0;
   std::size_t started_count_ = 0;
   application_state state_ = application_state::created;
};

} // namespace forge::app
