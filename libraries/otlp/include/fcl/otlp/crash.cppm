module;

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

export module fcl.otlp.crash;

export import fcl.otlp.exceptions;
export import fcl.otlp.log_exporter;

export namespace fcl::otlp {

[[nodiscard]] std::vector<int> default_crash_signals();

struct crash_spool_options {
   std::filesystem::path directory;
   std::size_t max_record_bytes = 4096;
   std::size_t max_records_per_process = 64;
   std::size_t max_records_per_resend = 1024;
   std::size_t max_file_bytes = 256 * 1024;
   bool capture_signals = true;
   bool capture_terminate = true;
   std::vector<int> signals = default_crash_signals();
};

struct crash_resend_result {
   std::uint64_t files_scanned = 0;
   std::uint64_t files_exported = 0;
   std::uint64_t files_retained = 0;
   std::uint64_t bad_files = 0;
   std::uint64_t records_read = 0;
   std::uint64_t exported_records = 0;
   std::uint64_t failed_records = 0;
};

class crash_guard {
 public:
   crash_guard() = default;
   ~crash_guard();

   crash_guard(const crash_guard&) = delete;
   crash_guard& operator=(const crash_guard&) = delete;

   crash_guard(crash_guard&& other) noexcept;
   crash_guard& operator=(crash_guard&& other) noexcept;

   [[nodiscard]] explicit operator bool() const noexcept;

 private:
   explicit crash_guard(std::shared_ptr<void> impl);

   std::shared_ptr<void> impl_;

   friend crash_guard install_crash_capture(crash_spool_options options);
};

[[nodiscard]] crash_guard install_crash_capture(crash_spool_options options);

boost::asio::awaitable<crash_resend_result> async_resend_crashes(log_exporter& exporter,
                                                                 crash_spool_options options);

} // namespace fcl::otlp
