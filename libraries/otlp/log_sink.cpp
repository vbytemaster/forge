module;

#include <memory>
#include <utility>

module fcl.otlp.log_sink;

namespace fcl::otlp {

log_sink::log_sink(std::shared_ptr<log_exporter> exporter) : exporter_(std::move(exporter)) {}

log_sink::~log_sink() = default;

void log_sink::log(const fcl::log_record& record) {
   if (exporter_) {
      static_cast<void>(exporter_->enqueue(record));
   }
}

} // namespace fcl::otlp
