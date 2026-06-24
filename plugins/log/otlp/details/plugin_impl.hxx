#pragma once

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <string>
#include <vector>

namespace forge::plugins::log::otlp {

struct attached_logger {
   std::string name;
   forge::logger logger;
};

struct plugin::impl {
   config settings;
   forge::asio::runtime* runtime = nullptr;
   std::shared_ptr<forge::otlp::log_exporter> exporter;
   std::shared_ptr<forge::otlp::log_sink> sink;
   forge::otlp::crash_guard crash_guard;
   std::vector<attached_logger> attached_loggers;
   forge::app::view_registry* views = nullptr;
   forge::app::view_registration metrics_view;
   bool started = false;
   bool stopping = false;

   [[nodiscard]] bool available() const noexcept;
   [[nodiscard]] metrics current_metrics() const;
   boost::asio::awaitable<void> flush();
   void detach_sink() noexcept;
};

boost::asio::awaitable<void> start_exporter(plugin::impl& state);
void request_exporter_stop(plugin::impl& state) noexcept;
boost::asio::awaitable<void> stop_exporter(plugin::impl& state);

} // namespace forge::plugins::log::otlp
