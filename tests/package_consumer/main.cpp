#include <forge/exceptions/macros.hpp>
#include <forge/log/macros.hpp>

#include <memory>
#include <string>

import forge.app.application;
import forge.crypto.sha256;
import forge.exceptions;
import forge.log.log_message;
import forge.log.logger;
import forge.log.record;
import forge.raw.raw;

class capture_sink final : public forge::sink {
 public:
   void log(const forge::log_record& record) override {
      last_message = record.message;
   }

   std::string last_message;
};

int main() {
   auto logger = forge::logger{"consumer"};
   logger.set_log_level(forge::log_level::debug);
   auto sink = std::make_shared<capture_sink>();
   logger.add_sink(sink);
   logger.info("package works", {forge::log_ctx("component", "smoke")});

   const auto digest = forge::crypto::sha256::hash(std::string{"package works"});
   const auto bytes = forge::raw::pack(std::string{digest});
   FORGE_ASSERT(!bytes.empty(), "raw pack should produce bytes", forge::exceptions::ctx("size", bytes.size()));

   return sink->last_message == "package works" ? 0 : 1;
}
