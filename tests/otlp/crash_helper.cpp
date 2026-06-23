#include <forge/exceptions/macros.hpp>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

import forge.asio.blocking;
import forge.asio.runtime;
import forge.exceptions;
import forge.otlp.exceptions;
import forge.otlp.options;
import forge.otlp.log_exporter;
import forge.otlp.log_sink;
import forge.otlp.crash;

namespace {

namespace test_errors {

enum class code : std::uint16_t {
   crash = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.otlp.test")

using crash = forge::exceptions::coded_exception<code, code::crash>;

} // namespace test_errors

using namespace std::chrono_literals;

forge::otlp::crash_spool_options make_options(const char* directory) {
   return forge::otlp::crash_spool_options{
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

forge::otlp::log_exporter_options make_exporter_options(const char* endpoint) {
   return forge::otlp::log_exporter_options{
       .endpoint = endpoint,
       .batch = {.max_records = 10, .max_bytes = 64 * 1024, .flush_interval = 1h},
       .queue = {.max_records = 100, .max_bytes = 1024 * 1024},
       .retry = {.max_attempts = 0, .base_delay = 1ms, .max_delay = 10ms},
       .request_timeout = 120s,
       .shutdown_timeout = 120s,
   };
}

int run_resend(const char* directory, const char* endpoint, const char* max_records) {
   auto runtime = forge::asio::runtime{};
   auto exporter = forge::otlp::log_exporter{runtime, make_exporter_options(endpoint)};
   auto options = make_options(directory);
   options.max_records_per_resend = static_cast<std::size_t>(std::stoull(max_records));
   (void)forge::asio::blocking::run(runtime, forge::otlp::async_resend_crashes(exporter, options));
   forge::asio::blocking::run(runtime, exporter.async_shutdown());
   return 0;
}

int run_hold_capture(const char* directory, const char* ready_path) {
   auto options = make_options(directory);
   options.capture_terminate = false;
   auto guard = forge::otlp::install_crash_capture(options);

   auto ready = std::ofstream{ready_path, std::ios::binary};
   ready << "ready";
   ready.close();

   while (true) {
      std::this_thread::sleep_for(1s);
   }
}

int run_hold_capture_after_marker(const char* directory, const char* go_path, const char* ready_path) {
   while (!std::filesystem::exists(go_path)) {
      std::this_thread::sleep_for(10ms);
   }

   return run_hold_capture(directory, ready_path);
}

} // namespace

int main(int argc, char** argv) {
   if (argc >= 2 && std::string{argv[1]} == "resend") {
      if (argc != 5) {
         return 2;
      }
      return run_resend(argv[2], argv[3], argv[4]);
   }
   if (argc >= 2 && std::string{argv[1]} == "hold_capture") {
      if (argc != 4) {
         return 2;
      }
      return run_hold_capture(argv[2], argv[3]);
   }
   if (argc >= 2 && std::string{argv[1]} == "hold_capture_after_marker") {
      if (argc != 5) {
         return 2;
      }
      return run_hold_capture_after_marker(argv[2], argv[3], argv[4]);
   }

   if (argc != 3) {
      return 2;
   }

   auto guard = forge::otlp::install_crash_capture(make_options(argv[2]));
   const auto mode = std::string{argv[1]};
   if (mode == "terminate") {
      FORGE_THROW_EXCEPTION(test_errors::crash, "super-secret-token",
                          forge::exceptions::secret("token", "super-secret-token"));
   }
   if (mode == "sigabrt") {
      std::raise(SIGABRT);
      return 3;
   }
   return 4;
}
