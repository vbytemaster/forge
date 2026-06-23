module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module forge.otlp.log_exporter;

export import forge.asio.runtime;
export import forge.log.record;
export import forge.otlp.exceptions;
export import forge.otlp.options;

export namespace forge::otlp {

class log_exporter {
 public:
   log_exporter(forge::asio::runtime& runtime, log_exporter_options options = {});
   ~log_exporter();

   log_exporter(const log_exporter&) = delete;
   log_exporter& operator=(const log_exporter&) = delete;

   [[nodiscard]] bool enqueue(const forge::log_record& record);
   [[nodiscard]] exporter_metrics metrics() const;

   boost::asio::awaitable<export_result> async_export(std::vector<forge::log_record> records);
   boost::asio::awaitable<void> async_flush();
   boost::asio::awaitable<void> async_shutdown();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::otlp
