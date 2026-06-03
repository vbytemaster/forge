#include <fcl/exceptions/macros.hpp>
#include <fcl/log/macros.hpp>

#include <memory>
#include <string>

import fcl.app.application;
import fcl.crypto.sha256;
import fcl.exceptions;
import fcl.log.log_message;
import fcl.log.logger;
import fcl.log.record;
import fcl.raw.raw;

class capture_sink final : public fcl::sink {
 public:
   void log(const fcl::log_record& record) override {
      last_message = record.message;
   }

   std::string last_message;
};

int main() {
   auto logger = fcl::logger{"consumer"};
   logger.set_log_level(fcl::log_level::debug);
   auto sink = std::make_shared<capture_sink>();
   logger.add_sink(sink);
   logger.info("package works", {fcl::log_ctx("component", "smoke")});

   const auto digest = fcl::crypto::sha256::hash(std::string{"package works"});
   const auto bytes = fcl::raw::pack(std::string{digest});
   FCL_ASSERT(!bytes.empty(), "raw pack should produce bytes", fcl::exceptions::ctx("size", bytes.size()));

   return sink->last_message == "package works" ? 0 : 1;
}
