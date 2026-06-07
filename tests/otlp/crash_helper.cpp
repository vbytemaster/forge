#include <fcl/exceptions/macros.hpp>

#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>

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

fcl::otlp::crash_spool_options make_options(const char* directory) {
   return fcl::otlp::crash_spool_options{
       .directory = std::filesystem::path{directory},
       .max_record_bytes = 4096,
       .max_records_per_process = 8,
       .max_records_per_resend = 1024,
       .max_file_bytes = 256 * 1024,
       .capture_signals = true,
       .capture_terminate = true,
   };
}

} // namespace

int main(int argc, char** argv) {
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
