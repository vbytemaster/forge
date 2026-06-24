module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <exception>
#include <memory>
#include <utility>

module forge.plugins.log.otlp.plugin;

import forge.asio.runtime;
import forge.app.views;
import forge.log.log_message;
import forge.log.logger;
import forge.otlp.crash;
import forge.otlp.log_exporter;
import forge.otlp.log_sink;
import forge.plugins.log.otlp.exceptions;
import forge.plugins.log.otlp.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"

namespace forge::plugins::log::otlp {

boost::asio::awaitable<void> start_exporter(plugin::impl& state) {
   if (state.started) {
      co_return;
   }
   state.started = true;
   state.stopping = false;

   if (!state.settings.enabled) {
      co_return;
   }
   if (state.runtime == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::startup_failed, "OTLP logs plugin was not initialized with a runtime");
   }

   try {
      state.exporter = std::make_shared<forge::otlp::log_exporter>(*state.runtime, make_exporter_options(state.settings));
      state.sink = std::make_shared<forge::otlp::log_sink>(state.exporter);
      const auto attach_route = [&state](const logger_route& route) {
         auto logger = forge::logger::get(route.name);
         logger.set_name(route.name);
         logger.set_enabled(route.enabled);
         logger.set_log_level(parse_log_level(route.level));
         if (route.export_logs) {
            logger.add_sink(state.sink);
            state.attached_loggers.push_back(attached_logger{.name = route.name, .logger = logger});
         }
         forge::logger::update(route.name, logger);
      };
      for (const auto& route : state.settings.loggers) {
         attach_route(route);
      }
      if (state.settings.crash_spool.enabled) {
         const auto crash_options = make_crash_spool_options(state.settings);
         state.crash_guard = forge::otlp::install_crash_capture(crash_options);
         if (state.settings.crash_spool.resend_on_startup) {
            co_await forge::otlp::async_resend_crashes(*state.exporter, crash_options);
         }
      }
   } catch (const exceptions::startup_failed&) {
      throw;
   } catch (const std::exception& error) {
      state.detach_sink();
      state.sink.reset();
      state.exporter.reset();
      state.started = false;
      FORGE_THROW_EXCEPTION(exceptions::startup_failed, "failed to start OTLP logs exporter",
                            forge::exceptions::ctx("error", error.what()));
   }
}

void request_exporter_stop(plugin::impl& state) noexcept {
   state.stopping = true;
}

boost::asio::awaitable<void> stop_exporter(plugin::impl& state) {
   state.stopping = true;
   state.crash_guard = forge::otlp::crash_guard{};

   auto exporter = std::move(state.exporter);
   if (exporter) {
      try {
         co_await exporter->async_shutdown();
      } catch (...) {
         state.detach_sink();
         state.sink.reset();
         state.started = false;
         throw;
      }
   }

   state.detach_sink();
   state.sink.reset();
   state.started = false;
}

} // namespace forge::plugins::log::otlp
