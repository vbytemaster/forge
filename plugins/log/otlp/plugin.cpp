module;

#include <boost/asio/awaitable.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

module forge.plugins.log.otlp.plugin;

import forge.api.registry;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.views;
import forge.config.component;
import forge.config.decode;
import forge.log.log_message;
import forge.log.logger;
import forge.otlp.crash;
import forge.otlp.log_exporter;
import forge.otlp.log_sink;
import forge.plugins.log.otlp.api;
import forge.plugins.log.otlp.exceptions;
import forge.plugins.log.otlp.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/management_api.hxx"

namespace forge::plugins::log::otlp {
namespace {

class metrics_view_source final : public forge::app::view_source {
 public:
   explicit metrics_view_source(std::function<metrics()> read_metrics) : read_metrics_{std::move(read_metrics)} {}

   boost::asio::awaitable<forge::app::view_snapshot> snapshot(forge::app::view_query) override {
      const auto current = read_metrics_();
      co_return forge::app::view_snapshot{
         .descriptor =
             {
                 .id = "forge.plugins.log.otlp.metrics",
                 .title = "OTLP Log Exporter",
                 .category = "logging",
                 .kind = forge::app::view_kind::counters,
             },
         .counters =
             {
                 {.name = "enqueued", .value = std::to_string(current.enqueued_records)},
                 {.name = "exported", .value = std::to_string(current.exported_records)},
                 {.name = "failed", .value = std::to_string(current.failed_records),
                  .severity = current.failed_records == 0 ? forge::app::view_severity::info
                                                          : forge::app::view_severity::error},
                 {.name = "dropped", .value = std::to_string(current.dropped_records),
                  .severity = current.dropped_records == 0 ? forge::app::view_severity::info
                                                           : forge::app::view_severity::warning},
                 {.name = "queue.records", .value = std::to_string(current.queue_records)},
                 {.name = "queue.bytes", .value = std::to_string(current.queue_bytes)},
             },
      };
   }

 private:
   std::function<metrics()> read_metrics_;
};

} // namespace

plugin::plugin() : impl_{std::make_shared<impl>()} {}

plugin::~plugin() = default;

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.log.otlp"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<forge::config::component_descriptor> plugin::describe_config() const {
   return forge::config::describe_component<config>("plugins.log.otlp");
}

boost::asio::awaitable<void> plugin::configure(forge::config::component_view view) {
   impl_->settings = decode_config(view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::provider& provider) {
   provider.install<api>(std::make_shared<management_api>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context& context) {
   impl_->runtime = &context.scheduler().runtime_context();
   impl_->views = &context.views();
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   co_await start_exporter(*impl_);
   if (impl_->settings.enabled && impl_->views != nullptr && !impl_->metrics_view.active()) {
      impl_->metrics_view = impl_->views->register_source(
         {
            .id = "forge.plugins.log.otlp.metrics",
            .title = "OTLP Log Exporter",
            .category = "logging",
            .kind = forge::app::view_kind::counters,
         },
         std::make_shared<metrics_view_source>([state = impl_] {
            return state->current_metrics();
         }));
   }
}

void plugin::request_stop() noexcept {
   request_exporter_stop(*impl_);
}

boost::asio::awaitable<void> plugin::shutdown() {
   impl_->metrics_view.unregister();
   co_await stop_exporter(*impl_);
   impl_->views = nullptr;
   impl_->runtime = nullptr;
}

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "forge.plugins.log.otlp"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace forge::plugins::log::otlp
