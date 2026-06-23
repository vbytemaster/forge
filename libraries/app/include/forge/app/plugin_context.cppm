module;

#include <map>
#include <optional>
#include <string>

export module forge.app.plugin_context;

import forge.app.diagnostics;
import forge.app.events;
import forge.app.signals;
import forge.asio.task_scheduler;
import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;

export namespace forge::app {

using config_view = std::map<std::string, std::string>;

class plugin_context {
 public:
   plugin_context(forge::asio::task_scheduler& scheduler, forge::api::registry& apis, signal_bus& signals,
                  event_bus& events, diagnostics_store* diagnostics = nullptr, config_view config = {});
   plugin_context(forge::asio::task_scheduler& scheduler, signal_bus& signals, event_bus& events,
                  diagnostics_store* diagnostics = nullptr, config_view config = {});

   [[nodiscard]] forge::asio::task_scheduler& scheduler() noexcept;
   [[nodiscard]] forge::api::view apis() const noexcept;
   [[nodiscard]] signal_bus& signals() noexcept;
   [[nodiscard]] event_bus& events() noexcept;
   [[nodiscard]] diagnostics_store* diagnostics() noexcept;
   [[nodiscard]] const config_view& config() const noexcept;
   [[nodiscard]] std::optional<std::string> config_value(const std::string& key) const;

 private:
   forge::asio::task_scheduler* scheduler_ = nullptr;
   forge::api::registry* apis_ = nullptr;
   signal_bus* signals_ = nullptr;
   event_bus* events_ = nullptr;
   diagnostics_store* diagnostics_ = nullptr;
   config_view config_;
};

} // namespace forge::app
