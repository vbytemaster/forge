#include <fcl/exceptions/macros.hpp>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>

import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.exceptions;
import fcl.otlp;

namespace {

namespace test_errors {

enum class code : std::uint16_t {
   crash = 1,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.otlp.test")

using crash = fcl::exceptions::coded_exception<code, code::crash>;

} // namespace test_errors

using namespace std::chrono_literals;

fcl::otlp::crash_spool_options make_options(const char* directory) {
   return fcl::otlp::crash_spool_options{
       .directory = std::filesystem::path{directory},
       .max_record_bytes = 4096,
       .max_records_per_process = 8,
       .max_records_per_resend = 1024,
       .max_file_bytes = 256 * 1024,
       .capture_signals = true,
       .capture_terminate = true,
       .chain_after_capture = false,
   };
}

fcl::otlp::log_exporter_options make_exporter_options(const char* endpoint) {
   return fcl::otlp::log_exporter_options{
       .endpoint = endpoint,
       .batch = {.max_records = 10, .max_bytes = 64 * 1024, .flush_interval = 1h},
       .queue = {.max_records = 100, .max_bytes = 1024 * 1024},
       .retry = {.max_attempts = 0, .base_delay = 1ms, .max_delay = 10ms},
       .request_timeout = 120s,
       .shutdown_timeout = 120s,
   };
}

int run_resend(const char* directory, const char* endpoint, const char* max_records) {
   auto runtime = fcl::asio::runtime{};
   auto exporter = fcl::otlp::log_exporter{runtime, make_exporter_options(endpoint)};
   auto options = make_options(directory);
   options.max_records_per_resend = static_cast<std::size_t>(std::stoull(max_records));
   (void)fcl::asio::blocking::run(runtime, fcl::otlp::async_resend_crashes(exporter, options));
   fcl::asio::blocking::run(runtime, exporter.async_shutdown());
   return 0;
}

} // namespace

int main(int argc, char** argv) {
   if (argc >= 2 && std::string{argv[1]} == "resend") {
      if (argc != 5) {
         return 2;
      }
      return run_resend(argv[2], argv[3], argv[4]);
   }

   if (argc != 3) {
      return 2;
   }

   auto guard = fcl::otlp::install_crash_capture(make_options(argv[2]));
   const auto mode = std::string{argv[1]};
   if (mode == "terminate") {
      FCL_THROW_EXCEPTION(test_errors::crash, "super-secret-token",
                          fcl::exceptions::secret("token", "super-secret-token"));
   }
   if (mode == "sigabrt") {
      std::raise(SIGABRT);
      return 3;
   }
   return 4;
}
