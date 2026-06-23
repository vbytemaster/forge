module;

#include <memory>

export module forge.otlp.log_sink;

export import forge.log.record;
export import forge.otlp.log_exporter;

export namespace forge::otlp {

class log_sink final : public forge::sink {
 public:
   explicit log_sink(std::shared_ptr<log_exporter> exporter);
   ~log_sink() override;

   void log(const forge::log_record& record) override;

 private:
   std::shared_ptr<log_exporter> exporter_;
};

} // namespace forge::otlp
