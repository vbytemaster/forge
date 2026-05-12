#include <fcl/exception/macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import fcl.exception.exception;
import fcl.log.log_message;
import fcl.log.logger;
import fcl.log.record;

namespace {

class capture_sink final : public fcl::sink {
 public:
   void log(const fcl::log_record& record) override {
      records.push_back(record);
   }

   std::vector<fcl::log_record> records;
};

} // namespace

int main() {
   auto logger = fcl::logger{"example.exception"};
   logger.set_log_level(fcl::log_level::debug);
   auto sink = std::make_shared<capture_sink>();
   logger.add_sink(sink);

   fcl::error::set_log_sink([&](std::string_view chain) {
      logger.error("startup failed", {fcl::log_ctx("exception_chain", chain), fcl::log_secret("token", "abc123")});
   });

   try {
      try {
         throw std::runtime_error{"inner failure"};
      }
      FCL_CAPTURE_AND_LOG("starting service", fcl::error::ctx("phase", "startup"),
                          fcl::error::secret("token", "abc123"))
   } catch (...) {
      return 2;
   }

   fcl::error::set_log_sink({});
   if (sink->records.size() != 1) {
      return 3;
   }
   const auto& record = sink->records.front();
   if (record.fields.front().value.find("inner failure") == std::string::npos) {
      return 4;
   }
   if (record.fields.front().value.find("abc123") != std::string::npos) {
      return 5;
   }
   return 0;
}
