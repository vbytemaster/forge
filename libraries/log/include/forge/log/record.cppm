module;
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module forge.log.record;

import forge.log.log_message;

export namespace forge {

struct log_field {
   std::string key;
   std::string value;
   bool redacted = false;
};

using log_fields = std::vector<log_field>;

struct log_field_provider {
   std::function<log_field()> provider;
};

log_field log_ctx(std::string_view key, std::string value);
log_field log_ctx(std::string_view key, std::string_view value);
log_field log_ctx(std::string_view key, const char* value);
log_field log_ctx(std::string_view key, bool value);
log_field log_ctx(std::string_view key, char value);
log_field log_ctx(std::string_view key, signed char value);
log_field log_ctx(std::string_view key, unsigned char value);
log_field log_ctx(std::string_view key, short value);
log_field log_ctx(std::string_view key, unsigned short value);
log_field log_ctx(std::string_view key, int value);
log_field log_ctx(std::string_view key, unsigned value);
log_field log_ctx(std::string_view key, long value);
log_field log_ctx(std::string_view key, unsigned long value);
log_field log_ctx(std::string_view key, long long value);
log_field log_ctx(std::string_view key, unsigned long long value);
log_field log_ctx(std::string_view key, float value);
log_field log_ctx(std::string_view key, double value);
log_field log_ctx(std::string_view key, long double value);

template <typename T> log_field log_ctx(std::string_view key, const T&) {
   return log_ctx(key, std::string{"<unprintable>"});
}

log_field log_secret(std::string_view key, std::string value);
log_field log_secret(std::string_view key, std::string_view value);
log_field log_secret(std::string_view key, const char* value);

template <typename T> log_field log_secret(std::string_view key, const T& value) {
   auto field = log_ctx(key, value);
   field.value = "<redacted>";
   field.redacted = true;
   return field;
}

void append_log_field(log_fields& fields, log_field field);
void append_log_field(log_fields& fields, const log_field_provider& provider);

template <typename... Fields> log_fields make_log_fields(Fields&&... fields) {
   log_fields result;
   result.reserve(sizeof...(Fields));
   (append_log_field(result, std::forward<Fields>(fields)), ...);
   return result;
}

struct stacktrace_frame {
   std::size_t index = 0;
   std::uintptr_t address = 0;
   std::string name;
   std::string source_file;
   std::uint64_t source_line = 0;
};

struct stacktrace_snapshot {
   std::string backend;
   std::string unavailable_reason;
   std::vector<stacktrace_frame> frames;

   [[nodiscard]] bool available() const noexcept {
      return !frames.empty();
   }
};

[[nodiscard]] stacktrace_snapshot capture_stacktrace(std::size_t skip = 0, std::size_t max_frames = 64);
[[nodiscard]] std::string format_stacktrace(const stacktrace_snapshot& stacktrace);

struct log_record {
   log_level level = log_level::info;
   std::string logger;
   std::string component;
   std::string message;
   log_fields fields;
   std::chrono::sys_time<std::chrono::microseconds> timestamp;
   std::string thread_id;
   std::string thread_name;
   std::source_location location;
   std::optional<stacktrace_snapshot> stacktrace;
   std::string exception_chain;
};

[[nodiscard]] std::string format_text_log_record(const log_record& record);
[[nodiscard]] std::string format_json_log_record(const log_record& record);

class sink {
 public:
   virtual ~sink() = default;
   virtual void log(const log_record& record) = 0;
};

class console_sink final : public sink {
 public:
   explicit console_sink(bool stderr_for_warnings = true);
   ~console_sink() override;
   void log(const log_record& record) override;

 private:
   bool stderr_for_warnings_ = true;
};

class file_sink final : public sink {
 public:
   explicit file_sink(std::filesystem::path path, bool append = true);
   ~file_sink() override;
   void log(const log_record& record) override;

 private:
   class impl;
   std::unique_ptr<impl> impl_;
};

class jsonl_sink final : public sink {
 public:
   explicit jsonl_sink(std::filesystem::path path, bool append = true);
   ~jsonl_sink() override;
   void log(const log_record& record) override;

 private:
   class impl;
   std::unique_ptr<impl> impl_;
};

} // namespace forge
