module;

#include <forge/exceptions/macros.hpp>

#include <chrono>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

module forge.plugins.log.otlp.plugin;

import forge.config.component;
import forge.config.decode;
import forge.http.base_url;
import forge.log.log_message;
import forge.otlp.options;
import forge.otlp.crash;
import forge.plugins.log.otlp.exceptions;
import forge.plugins.log.otlp.types;
import forge.variant.value;

#include "details/config.hxx"

namespace forge::plugins::log::otlp {
namespace {

std::chrono::milliseconds ms(std::uint64_t value) {
   return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value)};
}

forge::otlp::attribute to_otlp_attribute(const attribute& value) {
   return forge::otlp::attribute{.key = value.key, .value = value.value};
}

forge::otlp::attribute to_otlp_header(const header& value) {
   return forge::otlp::attribute{.key = value.name, .value = value.value};
}

void validate_endpoint(const std::string& value) {
   try {
      const auto parsed = forge::http::parse_base_url(value);
      if (parsed.scheme != "http" && parsed.scheme != "https") {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "OTLP logs endpoint must use http or https");
      }
   } catch (const exceptions::invalid_config&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "OTLP logs endpoint is invalid",
                            forge::exceptions::ctx("error", error.what()));
   }
}

void validate_logs_path(const std::string& value) {
   if (value.empty() || value.front() != '/') {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "OTLP logs path must be absolute");
   }
   for (const auto ch : value) {
      if (ch == '\r' || ch == '\n' || ch == '\0') {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "OTLP logs path contains an unsafe control byte");
      }
   }
}

void validate_logger_name(const std::string& value) {
   for (const auto ch : value) {
      const auto byte = static_cast<unsigned char>(ch);
      if (byte < 0x20U || byte == 0x7fU) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "OTLP logs logger name contains an unsafe control byte");
      }
   }
}

bool is_http_token_char(unsigned char value) noexcept {
   if (value >= '0' && value <= '9') {
      return true;
   }
   if (value >= 'A' && value <= 'Z') {
      return true;
   }
   if (value >= 'a' && value <= 'z') {
      return true;
   }
   switch (value) {
      case '!':
      case '#':
      case '$':
      case '%':
      case '&':
      case '\'':
      case '*':
      case '+':
      case '-':
      case '.':
      case '^':
      case '_':
      case '`':
      case '|':
      case '~':
         return true;
      default:
         return false;
   }
}

void validate_header_name(std::string_view value) {
   if (value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "OTLP logs headers.name must not be empty");
   }
   for (const auto ch : value) {
      if (!is_http_token_char(static_cast<unsigned char>(ch))) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                               "OTLP logs headers.name contains an unsafe byte",
                               forge::exceptions::ctx("headers.name", value));
      }
   }
}

void validate_header_value(const header& value) {
   for (const auto ch : value.value) {
      const auto byte = static_cast<unsigned char>(ch);
      if (byte < 0x20U || byte == 0x7fU) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                               "OTLP logs headers.value contains an unsafe control byte",
                               forge::exceptions::ctx("headers.name", value.name));
      }
   }
}

void validate_header(const header& value) {
   validate_header_name(value.name);
   validate_header_value(value);
}

} // namespace

forge::log_level parse_log_level(std::string_view value) {
   try {
      return forge::variant{std::string{value}}.as<forge::log_level>();
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "OTLP logs logger level is invalid",
                            forge::exceptions::ctx("level", value), forge::exceptions::ctx("error", error.what()));
   }
}

config decode_config(const forge::config::component_view& view) {
   auto decoded = forge::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                            forge::config::format_decode_diagnostics("invalid OTLP logs config",
                                                                      decoded.diagnostics));
   }
   validate_endpoint(decoded.value.endpoint);
   validate_logs_path(decoded.value.logs_path);
   for (const auto& route : decoded.value.loggers) {
      validate_logger_name(route.name);
      (void)parse_log_level(route.level);
   }
   for (const auto& header : decoded.value.headers) {
      validate_header(header);
   }
   return std::move(decoded.value);
}

forge::otlp::log_exporter_options make_exporter_options(const config& value) {
   auto options = forge::otlp::log_exporter_options{};
   options.endpoint = value.endpoint;
   options.logs_path = value.logs_path;
   options.resource.attributes.reserve(value.resource.attributes.size());
   for (const auto& item : value.resource.attributes) {
      options.resource.attributes.push_back(to_otlp_attribute(item));
   }
   options.headers.reserve(value.headers.size());
   for (const auto& item : value.headers) {
      options.headers.push_back(to_otlp_header(item));
   }
   options.batch.max_records = static_cast<std::size_t>(value.batch.max_records);
   options.batch.max_bytes = static_cast<std::size_t>(value.batch.max_bytes);
   options.batch.flush_interval = ms(value.batch.flush_interval_ms);
   options.queue.max_records = static_cast<std::size_t>(value.queue.max_records);
   options.queue.max_bytes = static_cast<std::size_t>(value.queue.max_bytes);
   options.retry.max_attempts = static_cast<std::size_t>(value.retry.max_attempts);
   options.retry.base_delay = ms(value.retry.base_delay_ms);
   options.retry.max_delay = ms(value.retry.max_delay_ms);
   options.request_timeout = ms(value.request_timeout_ms);
   options.shutdown_timeout = ms(value.shutdown_timeout_ms);
   return options;
}

forge::otlp::crash_spool_options make_crash_spool_options(const config& value) {
   auto options = forge::otlp::crash_spool_options{};
   options.directory = std::filesystem::path{value.crash_spool.directory};
   return options;
}

} // namespace forge::plugins::log::otlp
