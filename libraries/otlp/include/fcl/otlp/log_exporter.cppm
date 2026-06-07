module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module fcl.otlp.log_exporter;

export import fcl.asio.runtime;
export import fcl.log.record;
export import fcl.otlp.exceptions;
export import fcl.otlp.options;

export namespace fcl::otlp {

class log_exporter {
 public:
   log_exporter(fcl::asio::runtime& runtime, log_exporter_options options = {});
   ~log_exporter();

   log_exporter(const log_exporter&) = delete;
   log_exporter& operator=(const log_exporter&) = delete;

   [[nodiscard]] bool enqueue(const fcl::log_record& record);
   [[nodiscard]] exporter_metrics metrics() const;

   boost::asio::awaitable<void> async_flush();
   boost::asio::awaitable<void> async_shutdown();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace fcl::otlp
